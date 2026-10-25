// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// sbctl — the operator's command-line tool (ground side; host tooling, so
// stdio is used freely here — not flight code).
//
//   sbctl cdm FILE [--self ID] [--verified] [--json] [--now GPS_MS]
//       Read a CCSDS CDM and print exactly what each spacecraft running
//       Starboard would decide: the canonical encounter, Starboard-Pc, the
//       verdict and deciding rule, and the give-way burn + return plan.
//       Without --self both objects' perspectives are shown.
//       --verified  : assume both craft are registry-verified (otherwise the
//                     counterpart is unverified, per SPEC Table 8-1).
//   sbctl selftest  Run the flight built-in self-test and report each test.
//   sbctl version

#include "starboard_fsw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *rule_name(int r)
{
    switch (r)
    {
        case RULE_CAPABILITY: return "Rule 1 (capability)";
        case RULE_TIER:       return "Rule 2 (tier)";
        case RULE_OVERTAKING: return "Rule 3 (overtaking)";
        case RULE_STARBOARD:  return "Rule 4 (starboard)";
        case RULE_FAIR_COIN:  return "Rule 5 (fair coin)";
        case RULE_CASCADE:    return "cascade";
        default:              return "-";
    }
}

static const char *frame_name(SbFrame f)
{
    return (f == SB_FRAME_ITRF) ? "ITRF (frozen-Earth mapped)"
         : (f == SB_FRAME_TEME) ? "TEME" : "GCRF/EME2000";
}

static void id_str(const ObjectId *id, char out[13])
{
    (void)memcpy(out, id->c, 12U);
    out[12] = '\0';
}

static long read_file(const char *path, char **buf)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (f == NULL)
    {
        return -1;
    }
    (void)fseek(f, 0, SEEK_END);
    n = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if ((n < 0) || (n > (1L << 20)))
    {
        (void)fclose(f);
        return -1;
    }
    *buf = (char *)malloc((size_t)n + 1U);
    if ((*buf == NULL) || (fread(*buf, 1U, (size_t)n, f) != (size_t)n))
    {
        (void)fclose(f);
        return -1;
    }
    (void)fclose(f);
    return n;
}

static void print_decision(const SbCdm *cdm, const SbDecision *d, bool json,
                           bool last)
{
    char self[13];
    char other[13];
    const char *role = (d->role == SB_ROLE_GIVE_WAY)  ? "GIVE WAY"
                     : (d->role == SB_ROLE_STAND_ON) ? "STAND ON" : "NO ACTION";
    id_str(&cdm->obj[d->self_index].id, self);
    id_str(&cdm->obj[1U - d->self_index].id, other);
    if (json)
    {
        printf("    {\"self\": \"%s\", \"role\": \"%s\", \"rule\": %d, "
               "\"advisory\": %s, \"conjunction_id\": \"%016llX\", "
               "\"burn_required\": %s, \"burn_epoch_gps_ms\": %lld, "
               "\"dv_rtn_mps\": [%.6f, %.6f, %.6f], \"dv_mps\": %.6f, "
               "\"return_dv_mps\": %.6f, \"late\": %s}%s\n",
               self, role, d->rule, d->advisory ? "true" : "false",
               (unsigned long long)d->conjunction_id,
               d->plan.required ? "true" : "false",
               (long long)d->burn_epoch_ms, d->plan.dv_hill.x,
               d->plan.dv_hill.y, d->plan.dv_hill.z, d->plan.dv_mag,
               d->ret.total_dv, d->late ? "true" : "false", last ? "" : ",");
        return;
    }
    printf("\n  On board %s (%s):\n", self, cdm->obj[d->self_index].name);
    if (d->role == SB_ROLE_NONE)
    {
        printf("    no action: the encounter is not actionable "
               "(Pc < 1e-4 and miss >= 1024 m)\n");
        return;
    }
    printf("    verdict   : I %s for %s   [%s]%s\n", role, other,
           rule_name(d->rule), d->advisory ? "  ADVISORY (nobody trusted to act)"
                                           : "");
    printf("    conj id   : %016llX\n", (unsigned long long)d->conjunction_id);
    if (d->plan.required)
    {
        printf("    burn      : %.4f m/s at TCA-%.0f s   "
               "RTN [%+.4f, %+.4f, %+.4f] m/s%s\n",
               d->plan.dv_mag,
               (double)(d->tca_epoch_ms - d->burn_epoch_ms) / 1000.0,
               d->plan.dv_hill.x, d->plan.dv_hill.y, d->plan.dv_hill.z,
               d->late ? "  (LATE: lead below preference)" : "");
        printf("    effect    : miss at TCA %.1f m -> %.1f m (safe radius %.0f m)\n",
               d->miss_m, d->miss_m + d->plan.displacement_m, SB_R_SAFE_M);
        printf("    return    : %.4f m/s to regain the slot (mirror + phase)\n",
               d->ret.total_dv);
    }
    else if ((d->role == SB_ROLE_GIVE_WAY) &&
             (cdm->obj[d->self_index].maneuverable != SB_MAN_YES))
    {
        printf("    action    : cannot maneuver — ALERT OPERATORS "
               "(advisory verdict)\n");
    }
    else if (d->role == SB_ROLE_GIVE_WAY)
    {
        printf("    burn      : none required (already outside the margin)\n");
    }
    else
    {
        printf("    action    : hold course — predictability is the stand-on "
               "craft's job\n");
    }
}

static int cmd_cdm(int argc, char **argv)
{
    const char *path = NULL;
    const char *self_arg = NULL;
    bool json = false;
    bool verified = false;
    long long now = -1;
    char *text = NULL;
    long n;
    SbCdm cdm;
    uint32_t line = 0U;
    SbCdmStatus st;
    OrbitConjunction oc;
    int64_t tca_ms;
    bool found;
    int i;
    int first;
    int last;

    for (i = 2; i < argc; ++i)
    {
        if ((strcmp(argv[i], "--self") == 0) && ((i + 1) < argc))
        {
            self_arg = argv[++i];
        }
        else if ((strcmp(argv[i], "--now") == 0) && ((i + 1) < argc))
        {
            now = atoll(argv[++i]);
        }
        else if (strcmp(argv[i], "--json") == 0)
        {
            json = true;
        }
        else if (strcmp(argv[i], "--verified") == 0)
        {
            verified = true;
        }
        else
        {
            path = argv[i];
        }
    }
    if (path == NULL)
    {
        fprintf(stderr, "usage: sbctl cdm FILE [--self ID] [--verified] "
                        "[--json] [--now GPS_MS]\n");
        return 2;
    }
    n = read_file(path, &text);
    if (n < 0)
    {
        fprintf(stderr, "sbctl: cannot read %s\n", path);
        return 2;
    }
    st = sb_cdm_parse(text, (uint32_t)n, &cdm, &line);
    free(text);
    if (st != SB_CDM_OK)
    {
        if (json)
        {
            printf("{\"ok\": false, \"error\": \"%s\", \"line\": %u}\n",
                   sb_cdm_status_str(st), line);
        }
        else
        {
            printf("CDM REJECTED: %s%s", sb_cdm_status_str(st),
                   (line != 0U) ? " at line " : "");
            if (line != 0U)
            {
                printf("%u", line);
            }
            printf("\n  (fail-safe: a CDM that cannot be bound is never "
                   "treated as 'no conjunction')\n");
        }
        return 1;
    }
    found = sb_cdm_screen(&cdm, &oc, &tca_ms);
    if (now < 0)
    {
        now = (long long)(cdm.tca_gps_ms - (3LL * 3600LL * 1000LL));
    }

    if (json)
    {
        char a[13];
        char b[13];
        uint64_t pc_bits;
        (void)memcpy(&pc_bits, &oc.conj.pc, sizeof(pc_bits));
        id_str(&cdm.obj[0].id, a);
        id_str(&cdm.obj[1].id, b);
        printf("{\"ok\": true, \"message_id\": \"%s\", \"objects\": "
               "[\"%s\", \"%s\"],\n  \"found\": %s, \"tca_gps_ms\": %lld, "
               "\"miss_m\": %.3f, \"pc\": %.6e, \"pc_bits\": \"%016llX\", "
               "\"actionable\": %s,\n  \"decisions\": [\n",
               cdm.message_id, a, b, found ? "true" : "false",
               (long long)tca_ms, oc.conj.miss_distance, oc.conj.pc,
               (unsigned long long)pc_bits,
               sb_actionable(&oc.conj) ? "true" : "false");
    }
    else
    {
        char a[13];
        char b[13];
        id_str(&cdm.obj[0].id, a);
        id_str(&cdm.obj[1].id, b);
        printf("Starboard verdict for CDM %s (from %s)\n", cdm.message_id,
               cdm.originator);
        printf("  objects   : %s %s [%s]  vs  %s %s [%s]\n", a,
               cdm.obj[0].name, frame_name(cdm.obj[0].frame), b,
               cdm.obj[1].name, frame_name(cdm.obj[1].frame));
        printf("  provider  : miss %.1f m, Pc %s\n", cdm.cdm_miss_m,
               (cdm.cdm_pc >= 0.0) ? "given" : "not given");
        if (!found)
        {
            printf("  canonical : no conjunction inside the 8192 m screen "
                   "— nothing to do\n");
            return 0;
        }
        printf("  canonical : miss %.1f m, Starboard-Pc %.3e, TCA %+.3f s "
               "from the CDM's\n", oc.conj.miss_distance, oc.conj.pc,
               (double)(tca_ms - cdm.tca_gps_ms) / 1000.0);
        printf("  actionable: %s\n", sb_actionable(&oc.conj)
               ? "YES (Pc >= 1e-4 or miss < 1024 m)" : "no");
    }

    first = 0;
    last = 1;
    for (i = first; i <= last; ++i)
    {
        SbFswConfig cfg;
        SbCounterpart other;
        SbDecision d;
        char sid[13];
        const SbCdmObject *me = &cdm.obj[i];
        const SbCdmObject *them = &cdm.obj[1 - i];
        id_str(&me->id, sid);
        if ((self_arg != NULL) && (strncmp(sid, self_arg, 12U) != 0) &&
            (strcmp(me->designator, self_arg) != 0))
        {
            continue;
        }
        (void)memset(&cfg, 0, sizeof(cfg));
        cfg.self_id = me->id;
        cfg.self_tier = TIER_OPERATIONAL;
        cfg.self_can_maneuver = (me->maneuverable == SB_MAN_YES);
        cfg.operating_mode = SB_MODE_ADVISORY;
        cfg.burn_lead_ms = 2700000;          /* ~half an orbit */
        cfg.cmd_latency_ms = 60000;
        (void)memset(&other, 0, sizeof(other));
        other.used = true;
        other.id = them->id;
        other.tier = TIER_OPERATIONAL;
        other.can_maneuver = (them->maneuverable == SB_MAN_YES);
        other.trust = verified ? TRUST_VERIFIED : TRUST_ASSERTED;
        if (sb_fsw_decide(&cfg, &cdm, &other, (int64_t)now, &d) == SB_FSW_OK)
        {
            print_decision(&cdm, &d, json,
                           (i == last) || (self_arg != NULL));
        }
    }
    if (json)
    {
        printf("  ]}\n");
    }
    else if (!verified)
    {
        printf("\n  note: each craft trusts only itself here (no registry); "
               "if both give way,\n        Theorem 2 guarantees the burns "
               "add separation. Use --verified to see\n        the verdict "
               "both craft share once each verifies the other.\n");
    }
    return 0;
}


static int cmd_selftest(int argc, char **argv)
{
    static SbFsw ctx;
    SbFswConfig cfg;
    static const char *const names[11] = {
        "strict arithmetic: no FMA contraction",
        "strict arithmetic: no excess precision",
        "pinned transcendentals (canon_exp, canon_sincos)",
        "J2 conjunction search: TCA, miss, Pc bits",
        "rule chain + conjunction id (both perspectives)",
        "give-way burn reaches kappa*R_SAFE",
        "SHA-512 known answer",
        "Ed25519 RFC 8032 verify + reject",
        "CDM binding known answer",
        "canonical constant table CRC",
        "IEEE-754 rounding probes (soft-float corners)"
    };
    uint32_t f;
    uint32_t b;
    if ((argc > 2) && (strcmp(argv[2], "--print-kat") == 0))
    {
        printf("SB_CONSTANTS_CRC 0x%08Xu\nSB_KAT_CDM_FOLD 0x%016llXull\n"
               "SB_TRANSCENDENTAL_FOLD 0x%016llXull\n",
               sb_fsw_constants_crc(),
               (unsigned long long)sb_fsw_kat_cdm_fold(),
               (unsigned long long)sb_fsw_transcendental_fold());
        return 0;
    }
    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.self_id = sb_make_object_id("2026-001A");
    cfg.self_tier = TIER_OPERATIONAL;
    cfg.self_can_maneuver = true;
    cfg.operating_mode = SB_MODE_ADVISORY;
    (void)sb_fsw_init(&ctx, &cfg);
    f = ctx.selftest_failed;
    printf("Starboard built-in self-test (flight layer %u.%u.%u, ruleset "
           "%08X)\n", (SB_FSW_VERSION >> 24) & 0xFFU,
           (SB_FSW_VERSION >> 16) & 0xFFU, (SB_FSW_VERSION >> 8) & 0xFFU,
           SB_RULESET_ID);
    for (b = 0U; b < 11U; ++b)
    {
        printf("  %-52s %s\n", names[b], ((f >> b) & 1U) ? "FAIL" : "pass");
    }
    printf("%s -> mode %s\n", (f == 0U) ? "ALL PASS" : "SELF-TEST FAILED",
           (ctx.mode == SB_MODE_SAFE) ? "SAFE" : "ADVISORY");
    return (f == 0U) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if ((argc >= 2) && (strcmp(argv[1], "cdm") == 0))
    {
        return cmd_cdm(argc, argv);
    }
    if ((argc >= 2) && (strcmp(argv[1], "selftest") == 0))
    {
        return cmd_selftest(argc, argv);
    }
    if ((argc >= 2) && (strcmp(argv[1], "version") == 0))
    {
        printf("sbctl %u.%u.%u  ruleset SB/1 %08X\n",
               (SB_FSW_VERSION >> 24) & 0xFFU, (SB_FSW_VERSION >> 16) & 0xFFU,
               (SB_FSW_VERSION >> 8) & 0xFFU, SB_RULESET_ID);
        return 0;
    }
    fprintf(stderr,
            "sbctl — Starboard operator tool\n"
            "  sbctl cdm FILE [--self ID] [--verified] [--json]   decide a CDM\n"
            "  sbctl selftest                                    flight BIST\n"
            "  sbctl version\n");
    return 2;
}
