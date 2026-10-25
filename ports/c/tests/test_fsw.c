// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — flight integration layer tests.
// Copyright (C) 2026 James H. All rights reserved.
//
// Exercises ports/c/fsw.c + cdm.c + ccsds.c end to end, including fault
// injection (build with -DSB_FSW_FAULT_INJECTION): single-event upsets in the
// decision datapath must be corrected by the 2-of-3 vote, double upsets must
// be contained (fault reported, nothing commanded), and corruption of a
// stored CDM between ingest and re-evaluation must be detected by its CRC.

#include "../starboard_fsw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(name, cond)                                                     \
    do {                                                                      \
        if (cond) { ++g_pass; }                                               \
        else { ++g_fail; printf("  FAIL  %s  (%s:%d)\n", name, __FILE__,      \
                                __LINE__); }                                  \
    } while (0)

#ifdef SB_FSW_FAULT_INJECTION
extern uint32_t sb_fi_run;
extern uint32_t sb_fi_flip_mask;
#endif

static char *slurp(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (f == NULL) { return NULL; }
    (void)fseek(f, 0, SEEK_END);
    n = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1U);
    if ((b == NULL) || (fread(b, 1U, (size_t)n, f) != (size_t)n)) {
        (void)fclose(f);
        free(b);
        return NULL;
    }
    (void)fclose(f);
    *len = (uint32_t)n;
    return b;
}

static SbFswConfig config_for(const char *self, SbMode mode)
{
    SbFswConfig c;
    (void)memset(&c, 0, sizeof(c));
    c.self_id = sb_make_object_id(self);
    c.self_tier = TIER_OPERATIONAL;
    c.self_can_maneuver = true;
    c.operating_mode = mode;
    c.apid_base = 0x3A0U;
    c.burn_lead_ms = 2700000;
    c.cmd_latency_ms = 60000;
    return c;
}

/* Parse a TM packet back and check the CCSDS framing + CRC. */
static bool tm_ok(const uint8_t *p, uint32_t n, uint16_t *apid)
{
    uint32_t dlen;
    uint16_t crc;
    if (n < 16U) { return false; }
    if ((p[0] & 0xE0U) != 0x00U) { return false; }          /* version 0  */
    if ((p[0] & 0x10U) != 0x00U) { return false; }          /* TM         */
    if ((p[0] & 0x08U) == 0x00U) { return false; }          /* sec header */
    if ((p[2] & 0xC0U) != 0xC0U) { return false; }          /* unsegmented*/
    dlen = ((uint32_t)p[4] << 8) | p[5];
    if ((dlen + 7U) != n) { return false; }
    crc = sb_crc16_ccitt(p, n - 2U);
    if ((p[n - 2U] != (uint8_t)(crc >> 8)) || (p[n - 1U] != (uint8_t)crc)) {
        return false;
    }
    *apid = (uint16_t)(((uint16_t)(p[0] & 0x07U) << 8) | p[1]);
    return true;
}

static void drain(SbFsw *ctx, int64_t now, uint32_t *hk, uint32_t *ev,
                  uint32_t *mn, bool *framing_ok)
{
    uint8_t buf[SB_FSW_TM_MAX_BYTES];
    uint32_t n;
    *hk = 0U; *ev = 0U; *mn = 0U; *framing_ok = true;
    while ((n = sb_fsw_poll_tm(ctx, buf, sizeof(buf), now)) != 0U) {
        uint16_t apid = 0U;
        if (!tm_ok(buf, n, &apid)) { *framing_ok = false; }
        if (apid == ctx->cfg.apid_base) { ++*hk; }
        else if (apid == (uint16_t)(ctx->cfg.apid_base + 1U)) { ++*ev; }
        else if (apid == (uint16_t)(ctx->cfg.apid_base + 2U)) { ++*mn; }
        else { *framing_ok = false; }
    }
}

int main(int argc, char **argv)
{
    static SbFsw ctx;
    const char *dir = (argc > 1) ? argv[1] : "../../examples/cdm";
    char path[512];
    uint32_t len = 0U;
    char *cdm;
    int64_t now;
    SbFswConfig cfg;
    uint32_t hk, ev, mn, slot = 0U;
    bool fr;
    SbCdm parsed;

    printf("Starboard flight-layer tests\n");

    /* --- CCSDS + CRC known answers ------------------------------------- */
    {
        const uint8_t s[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        CHECK("CRC-16/CCITT-FALSE check value", sb_crc16_ccitt(s, 9U) == 0x29B1U);
        CHECK("CRC-32 check value", sb_crc32(s, 9U) == 0xCBF43926U);
    }

    /* --- boot + self-test ------------------------------------------------ */
    cfg = config_for("2026-910C", SB_MODE_ADVISORY);
    CHECK("init passes self-test", sb_fsw_init(&ctx, &cfg) == SB_FSW_OK);
    CHECK("mode ADVISORY after boot", ctx.mode == SB_MODE_ADVISORY);
    CHECK("self-test mask clean", ctx.selftest_failed == 0U);

    /* --- ingest a real-format CDM in ADVISORY ---------------------------- */
    (void)snprintf(path, sizeof(path), "%s/shell_vs_debris.cdm", dir);
    cdm = slurp(path, &len);
    CHECK("example CDM readable", cdm != NULL);
    if (cdm == NULL) { return 1; }
    CHECK("parse", sb_cdm_parse(cdm, len, &parsed, NULL) == SB_CDM_OK);
    now = parsed.tca_gps_ms - 6LL * 3600LL * 1000LL;
    CHECK("ingest", sb_fsw_ingest_cdm(&ctx, cdm, len, now, &slot) == SB_FSW_OK);
    CHECK("decided give-way (debris unverified)",
          ctx.enc[slot].decision.role == SB_ROLE_GIVE_WAY);
    CHECK("burn planned", ctx.enc[slot].decision.plan.required);
    CHECK("burn lead = configured", ctx.enc[slot].decision.burn_epoch_ms ==
          (ctx.enc[slot].decision.tca_epoch_ms - cfg.burn_lead_ms));
    drain(&ctx, now, &hk, &ev, &mn, &fr);
    CHECK("TM framing + CRC valid", fr);
    CHECK("ADVISORY: event, no maneuver request", (ev == 1U) && (mn == 0U));

    /* --- the same CDM again is a rolling update, not a new encounter ----- */
    CHECK("re-ingest", sb_fsw_ingest_cdm(&ctx, cdm, len, now, &hk) == SB_FSW_OK);
    CHECK("rolling CDM reuses slot", hk == slot);

    /* --- AUTONOMOUS emits the maneuver request --------------------------- */
    CHECK("mode -> AUTONOMOUS", sb_fsw_set_mode(&ctx, SB_MODE_AUTONOMOUS) ==
          SB_FSW_OK);
    CHECK("ingest (autonomous)",
          sb_fsw_ingest_cdm(&ctx, cdm, len, now, &slot) == SB_FSW_OK);
    drain(&ctx, now, &hk, &ev, &mn, &fr);
    CHECK("AUTONOMOUS: HK + event + maneuver request",
          fr && (hk == 1U) && (ev == 1U) && (mn == 1U));

    /* --- Layer 4: the give-way craft's notice, and the silent flock ----- */
    {
        uint8_t w1[SB_NOTICE_BYTES];
        uint8_t w2[SB_NOTICE_BYTES];
        SbNotice got;
        SbNotice indep;
        SbDecision nd;
        OrbitConjunction noc;
        int64_t ntca;
        CHECK("flight layer emits a notice for its burn",
              sb_fsw_notice(&ctx, slot, w1) == SB_NOTICE_BYTES);
        CHECK("notice decodes", sb_notice_decode(w1, SB_NOTICE_BYTES, &got));
        /* A NEIGHBOUR holding the same CDM recomputes the mover's notice
         * from shared data alone — no message needed — bit for bit. */
        (void)sb_fsw_decide(&ctx.cfg, &parsed, NULL, now, &nd);
        (void)sb_cdm_screen(&parsed, &noc, &ntca);
        sb_notice_from_plan(&ctx.cfg.self_id, &parsed.obj[1].id, got.seq,
                            nd.conjunction_id, nd.tca_epoch_ms,
                            nd.burn_epoch_ms, noc.a_tca, &nd.plan, &nd.ret,
                            &indep);
        (void)sb_notice_encode(&indep, w2);
        CHECK("silent flock: neighbour's prediction == mover's notice",
              memcmp(w1, w2, SB_NOTICE_BYTES) == 0);
    }

    /* --- registry outcome flips the verdict (re-decided in place) ------- */
    {
        const ObjectId deb = parsed.obj[1].id;
        const SbDecision before = ctx.enc[slot].decision;
        CHECK("set counterpart VERIFIED CREWED",
              sb_fsw_set_counterpart(&ctx, deb, TRUST_VERIFIED, TIER_CREWED,
                                     true, now) == SB_FSW_OK);
        CHECK("still give-way, now by Rule 2 (tier)",
              (ctx.enc[slot].decision.role == SB_ROLE_GIVE_WAY) &&
              (ctx.enc[slot].decision.rule == RULE_TIER) &&
              (before.rule == RULE_CAPABILITY));
        CHECK("counterpart verified + unable -> I give way by Rule 1",
              (sb_fsw_set_counterpart(&ctx, deb, TRUST_VERIFIED,
                                      TIER_OPERATIONAL, false, now) ==
               SB_FSW_OK) &&
              (ctx.enc[slot].decision.rule == RULE_CAPABILITY));
    }

    /* --- fail-safe rejections ------------------------------------------- */
    {
        char *bad = (char *)malloc(len + 64U);
        char *p;
        uint32_t before = ctx.cdm_rejected;
        (void)memcpy(bad, cdm, len);
        p = strstr(bad, "CR_R");
        if (p != NULL) {
            char *eq = strchr(p, '=');
            (void)memcpy(eq + 2, "NaN      ", 9U);   /* not a number */
        }
        CHECK("NaN field -> CDM rejected (never 'clear')",
              sb_fsw_ingest_cdm(&ctx, bad, len, now, NULL) == SB_FSW_ERR_CDM);
        CHECK("reject counted + error latched",
              (ctx.cdm_rejected == before + 1U) &&
              (ctx.last_error == (uint8_t)SB_FSW_ERR_CDM) &&
              (ctx.last_cdm_error == (uint8_t)SB_CDM_ERR_NUMBER));
        (void)memcpy(bad, cdm, len);
        p = strstr(bad, "[km/s]");
        if (p != NULL) { (void)memcpy(p, "[m/s] ", 6U); }
        CHECK("wrong units -> rejected",
              sb_fsw_ingest_cdm(&ctx, bad, len, now, NULL) == SB_FSW_ERR_CDM);
        CHECK("stale TCA -> rejected",
              sb_fsw_ingest_cdm(&ctx, cdm, len,
                                parsed.tca_gps_ms + 1000, NULL) ==
              SB_FSW_ERR_STALE);
        free(bad);
    }
    {
        static SbFsw other;
        SbFswConfig oc = config_for("1999-999Z", SB_MODE_ADVISORY);
        (void)sb_fsw_init(&other, &oc);
        CHECK("CDM not naming this craft -> rejected",
              sb_fsw_ingest_cdm(&other, cdm, len, now, NULL) ==
              SB_FSW_ERR_NOT_INVOLVED);
    }

    /* --- stored-input corruption is detected by the CRC ------------------ */
    {
        const ObjectId deb = parsed.obj[1].id;
        uint32_t c0 = ctx.input_corruptions;
        uint8_t *raw = (uint8_t *)&ctx.enc[slot].cdm.obj[1].state_tca;
        raw[3] ^= 0x10U;                        /* an upset in RAM */
        CHECK("corrupted input detected on re-evaluation",
              (sb_fsw_set_counterpart(&ctx, deb, TRUST_VERIFIED,
                                      TIER_OPERATIONAL, true, now) ==
               SB_FSW_ERR_FAULT) &&
              (ctx.input_corruptions == c0 + 1U) && (!ctx.enc[slot].used));
    }

#ifdef SB_FSW_FAULT_INJECTION
    /* --- SEU injection in the redundant evaluation ---------------------- */
    {
        uint32_t c0 = ctx.seu_corrected;
        uint32_t u0 = ctx.seu_uncorrected;
        SbDecision clean;
        SbCounterpart cp;   /* what the context now holds for the debris */
        (void)memset(&cp, 0, sizeof(cp));
        cp.used = true;
        cp.id = parsed.obj[1].id;
        cp.trust = TRUST_VERIFIED;
        cp.tier = TIER_OPERATIONAL;
        cp.can_maneuver = true;
        (void)sb_fsw_decide(&ctx.cfg, &parsed, &cp, now, &clean);

        sb_fi_run = 0U; sb_fi_flip_mask = (1U << 1);   /* upset run 1 */
        CHECK("SEU in run 1: corrected",
              (sb_fsw_ingest_cdm(&ctx, cdm, len, now, &slot) == SB_FSW_OK) &&
              (ctx.seu_corrected == c0 + 1U) &&
              sb_fsw_decision_equal(&ctx.enc[slot].decision, &clean));
        sb_fi_run = 0U; sb_fi_flip_mask = (1U << 2);   /* upset run 2 */
        CHECK("SEU in run 2: corrected",
              (sb_fsw_ingest_cdm(&ctx, cdm, len, now, &slot) == SB_FSW_OK) &&
              (ctx.seu_corrected == c0 + 2U) &&
              sb_fsw_decision_equal(&ctx.enc[slot].decision, &clean));
        sb_fi_run = 0U; sb_fi_flip_mask = (1U << 1) | (1U << 2) | (1U << 3);
        drain(&ctx, now, &hk, &ev, &mn, &fr);
        CHECK("upsets in all runs: contained as FAULT",
              (sb_fsw_ingest_cdm(&ctx, cdm, len, now, &slot) ==
               SB_FSW_ERR_FAULT) && (ctx.seu_uncorrected == u0 + 1U));
        drain(&ctx, now, &hk, &ev, &mn, &fr);
        CHECK("FAULT commands nothing", (mn == 0U) && (ev == 0U) && (hk == 1U));
        sb_fi_flip_mask = 0U;
    }
#else
    printf("  (fault-injection tests skipped: build with "
           "-DSB_FSW_FAULT_INJECTION)\n");
#endif

    /* --- SAFE mode refuses to decide ------------------------------------ */
    CHECK("command SAFE", sb_fsw_set_mode(&ctx, SB_MODE_SAFE) == SB_FSW_OK);
    CHECK("SAFE: ingest refused",
          sb_fsw_ingest_cdm(&ctx, cdm, len, now, NULL) == SB_FSW_ERR_MODE);
    CHECK("leave SAFE (self-test re-run)",
          sb_fsw_set_mode(&ctx, SB_MODE_ADVISORY) == SB_FSW_OK);

    /* --- every example CDM, both perspectives, decides deterministically */
    {
        static const char *const names[3] = {
            "crossing_demo.cdm", "shell_vs_debris.cdm", "itrf_rocket_body.cdm"};
        int i;
        for (i = 0; i < 3; ++i) {
            uint32_t l2 = 0U;
            char *t;
            SbCdm c;
            (void)snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
            t = slurp(path, &l2);
            if ((t == NULL) || (sb_cdm_parse(t, l2, &c, NULL) != SB_CDM_OK)) {
                CHECK("example parses", false);
                free(t);
                continue;
            }
            {
                SbFswConfig ca = config_for("", SB_MODE_ADVISORY);
                SbFswConfig cb = ca;
                SbCounterpart pa, pb;
                SbDecision da, db;
                ca.self_id = c.obj[0].id;
                cb.self_id = c.obj[1].id;
                (void)memset(&pa, 0, sizeof(pa));
                pa.used = true; pa.id = c.obj[1].id; pa.trust = TRUST_VERIFIED;
                pa.tier = TIER_OPERATIONAL; pa.can_maneuver = true;
                pb = pa; pb.id = c.obj[0].id;
                (void)sb_fsw_decide(&ca, &c, &pa, c.tca_gps_ms - 7200000, &da);
                (void)sb_fsw_decide(&cb, &c, &pb, c.tca_gps_ms - 7200000, &db);
                CHECK("both verified: exactly one gives way, same id",
                      (da.conjunction_id == db.conjunction_id) &&
                      (da.rule == db.rule) &&
                      ((da.role == SB_ROLE_GIVE_WAY) !=
                       (db.role == SB_ROLE_GIVE_WAY)));
                if (da.plan.required || db.plan.required) {
                    const SbDecision *g = da.plan.required ? &da : &db;
                    CHECK("plan reaches kappa*R_SAFE in CW",
                          (g->miss_m + g->plan.displacement_m) >
                          (SB_MANEUVER_MARGIN * SB_R_SAFE_M - 1.0e-3));
                }
            }
            free(t);
        }
    }

    free(cdm);
    printf("%s: %d passed, %d failed\n", (g_fail == 0) ? "ALL PASS" : "FAILED",
           g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
