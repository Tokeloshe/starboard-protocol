// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — the murmuration demo (Layer 4, the flock).
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// Starlings in a murmuration never hold a meeting: each bird watches its
// neighbours, and a turn ripples through the flock. This demo shows Starboard
// doing the same thing with satellites, using the C99 flight library only:
//
//   Act I  — a 72-satellite shell (6 planes x 12). A dead fragment crosses
//            one satellite. That satellite decides — alone, from the shared
//            CDM — to give way, and announces its maneuver. The notice
//            ripples over the inter-satellite links; hop by hop every
//            satellite learns of it, and all 72 flock ledgers converge to the
//            same picture. Every satellite checks the mover's new path
//            against its own: nobody is endangered.
//   Act II — a tight formation (an orbital data-centre cluster, 8 craft
//            2 km apart on one orbit). Same threat. Now the give-way craft's
//            drift and return sweep through its formation-mates' slots — and
//            the flock SEES it: exactly the affected neighbours raise a FLOCK
//            ALERT, and every member agrees on which. The mover then checks
//            the alternatives against the flock and flies the return that
//            keeps the whole formation clear.
//
//   Build + run:  make -C ports/c murmuration && ports/c/murmuration
//
// Host demo: libm sin/cos build the scenario (off the verdict path).

#include "starboard_flock.h"
#include "starboard_fsw.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_SATS 80
#define T_TCA_MS (1477000000000ll)       /* GPS ms of the encounter */

typedef struct {
    ObjectId   id;
    char       name[16];
    CraftState tca_state;                /* canonical state at T_TCA */
    SbFlock    ledger;
    int        nbr[4];
    int        n_nbr;
    int        got_round;                /* round the notice arrived, -1 */
    uint8_t    inbox[4][SB_NOTICE_BYTES];
    int        inbox_n;
} Sat;

static Sat g_sat[MAX_SATS];

static CraftState circular(double r, double incl, double raan, double u)
{
    const double v = sqrt(SB_MU_EARTH_M3S2 / r);
    CraftState s;
    s.position.x = r * (cos(raan) * cos(u) - sin(raan) * sin(u) * cos(incl));
    s.position.y = r * (sin(raan) * cos(u) + cos(raan) * sin(u) * cos(incl));
    s.position.z = r * (sin(u) * sin(incl));
    s.velocity.x = v * (-cos(raan) * sin(u) - sin(raan) * cos(u) * cos(incl));
    s.velocity.y = v * (-sin(raan) * sin(u) + cos(raan) * cos(u) * cos(incl));
    s.velocity.z = v * (cos(u) * sin(incl));
    return sb_canonicalize(s);
}

/* A shared CDM for (sat, debris) built directly at TCA, as a provider would
 * publish it: the debris crosses the satellite with the given miss. */
static SbCdm make_cdm(const Sat *s, const char *deb_id, double miss_m)
{
    SbCdm c;
    const Vec3 rhat = sb_vnormalized(s->tca_state.position);
    const Vec3 vhat = sb_vnormalized(s->tca_state.velocity);
    const Vec3 chat = sb_vcross(rhat, vhat);
    (void)memset(&c, 0, sizeof(c));
    c.tca_gps_ms = T_TCA_MS;
    c.hbr_m = 20.0;
    c.obj[0].id = s->id;
    c.obj[0].maneuverable = SB_MAN_YES;
    c.obj[0].state_tca = s->tca_state;
    c.obj[0].covariance.xx = 400.0;
    c.obj[0].covariance.yy = 400.0;
    c.obj[0].covariance.zz = 400.0;
    c.obj[1].id = sb_make_object_id(deb_id);
    c.obj[1].maneuverable = SB_MAN_UNKNOWN;
    c.obj[1].object_type = SB_OBJ_DEBRIS;
    /* Debris on a steep crossing track, miss along the radial. */
    c.obj[1].state_tca.position =
        sb_vadd(s->tca_state.position, sb_vscale(rhat, miss_m));
    c.obj[1].state_tca.velocity =
        sb_vscale(sb_vnormalized(sb_vadd(sb_vscale(vhat, -0.3),
                                         sb_vscale(chat, 0.95))),
                  sb_vnorm(s->tca_state.velocity));
    c.obj[1].state_tca = sb_canonicalize(c.obj[1].state_tca);
    c.obj[1].covariance.xx = 2500.0;
    c.obj[1].covariance.yy = 2500.0;
    c.obj[1].covariance.zz = 2500.0;
    return c;
}

static void link(int a, int b)
{
    g_sat[a].nbr[g_sat[a].n_nbr++] = b;
}

/* Decide on board the threatened satellite and build its notice. */
static bool decide_and_announce(const Sat *s, const SbCdm *cdm, int64_t now,
                                SbDecision *d, SbNotice *n,
                                uint8_t wire[SB_NOTICE_BYTES])
{
    SbFswConfig cfg;
    OrbitConjunction oc;
    int64_t tca;
    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.self_id = s->id;
    cfg.self_tier = TIER_OPERATIONAL;
    cfg.self_can_maneuver = true;
    cfg.operating_mode = SB_MODE_AUTONOMOUS;
    cfg.burn_lead_ms = 2700000;
    cfg.cmd_latency_ms = 60000;
    if ((sb_fsw_decide(&cfg, cdm, NULL, now, d) != SB_FSW_OK) ||
        (d->role != SB_ROLE_GIVE_WAY) || !d->plan.required)
    {
        return false;
    }
    (void)sb_cdm_screen(cdm, &oc, &tca);
    sb_notice_from_plan(&s->id, &cdm->obj[1].id, 1u, d->conjunction_id,
                        d->tca_epoch_ms, d->burn_epoch_ms, oc.a_tca, &d->plan,
                        &d->ret, n);
    (void)sb_notice_encode(n, wire);
    return true;
}

/* Gossip: each round, every satellite forwards the notices it learned last
 * round to its link neighbours. Returns rounds to full convergence. */
static int ripple(int n_sats, int origin, const uint8_t wire[SB_NOTICE_BYTES],
                  bool verbose)
{
    int round = 0;
    int i;
    int msgs = 0;
    int holders = 1;
    uint8_t out[MAX_SATS][SB_NOTICE_BYTES];
    bool has_out[MAX_SATS];
    (void)memset(has_out, 0, sizeof(has_out));
    for (i = 0; i < n_sats; ++i)
    {
        g_sat[i].got_round = -1;
    }
    g_sat[origin].got_round = 0;
    (void)memcpy(out[origin], wire, SB_NOTICE_BYTES);
    has_out[origin] = true;
    while (holders < n_sats)
    {
        int j;
        bool next_out[MAX_SATS];
        uint8_t nbuf[MAX_SATS][SB_NOTICE_BYTES];
        int new_this_round = 0;
        (void)memset(next_out, 0, sizeof(next_out));
        ++round;
        for (i = 0; i < n_sats; ++i)
        {
            if (!has_out[i])
            {
                continue;
            }
            for (j = 0; j < g_sat[i].n_nbr; ++j)
            {
                const int k = g_sat[i].nbr[j];
                uint8_t relay[SB_NOTICE_BYTES];
                uint32_t rl = 0u;
                const SbFlockResult r =
                    sb_flock_receive(&g_sat[k].ledger, out[i], SB_NOTICE_BYTES,
                                     relay, &rl);
                ++msgs;
                if (((r == SB_FLOCK_NEW) || (r == SB_FLOCK_UPDATED)) &&
                    (g_sat[k].got_round < 0))
                {
                    g_sat[k].got_round = round;
                    ++holders;
                    ++new_this_round;
                    if (rl != 0u)
                    {
                        (void)memcpy(nbuf[k], relay, SB_NOTICE_BYTES);
                        next_out[k] = true;
                    }
                }
            }
        }
        (void)memcpy(has_out, next_out, sizeof(has_out));
        (void)memcpy(out, nbuf, sizeof(out));
        if (verbose)
        {
            char bar[MAX_SATS + 1];
            int b = 0;
            for (i = 0; i < n_sats; ++i)
            {
                bar[b++] = (g_sat[i].got_round < 0) ? '.'
                         : (g_sat[i].got_round == round) ? '*' : '#';
            }
            bar[b] = '\0';
            printf("   hop %2d  %s  %2d/%d know\n", round, bar, holders, n_sats);
        }
        if (new_this_round == 0)
        {
            break;                                  /* disconnected graph */
        }
    }
    if (verbose)
    {
        printf("   (%d link messages; duplicates are dropped, never relayed)\n",
               msgs);
    }
    return round;
}

static bool ledgers_agree(int n_sats, int origin, uint64_t *digest)
{
    /* The origin does not store its own notice; compare everyone else. */
    int i;
    bool first = true;
    bool same = true;
    for (i = 0; i < n_sats; ++i)
    {
        if (i == origin)
        {
            continue;
        }
        if (first)
        {
            *digest = sb_flock_digest(&g_sat[i].ledger);
            first = false;
        }
        else if (sb_flock_digest(&g_sat[i].ledger) != *digest)
        {
            same = false;
        }
    }
    return same;
}

static void act1(void)
{
    const int P = 6;
    const int S = 12;
    const int n = P * S;
    const double r = SB_R_EARTH_M + 550.0e3;
    const double inc = 53.0 * SB_PI / 180.0;
    int p;
    int k;
    int i;
    SbDecision d;
    SbNotice nt;
    uint8_t wire[SB_NOTICE_BYTES];
    SbCdm cdm;
    uint64_t dig = 0u;
    int conflicts = 0;
    double closest = 1.0e300;
    int closest_i = -1;

    printf("\n Act I - a 72-satellite shell (6 planes x 12, 550 km, 53 deg)\n\n");
    for (p = 0; p < P; ++p)
    {
        for (k = 0; k < S; ++k)
        {
            Sat *s = &g_sat[p * S + k];
            char id[16];
            (void)memset(s, 0, sizeof(*s));
            (void)snprintf(id, sizeof(id), "2026-3%02d%c", p, (char)('A' + k));
            (void)snprintf(s->name, sizeof(s->name), "P%dS%02d", p, k);
            s->id = sb_make_object_id(id);
            /* Walker-style phasing; at T_TCA each sat sits here. */
            s->tca_state = circular(r, inc, 2.0 * SB_PI * p / P,
                                    2.0 * SB_PI * k / S + SB_PI * p / (P * S));
            sb_flock_init(&s->ledger, s->id);
        }
    }
    for (p = 0; p < P; ++p)
    {
        for (k = 0; k < S; ++k)
        {
            const int me = p * S + k;
            link(me, p * S + (k + 1) % S);              /* in-plane ahead   */
            link(me, p * S + (k + S - 1) % S);          /* in-plane behind  */
            link(me, ((p + 1) % P) * S + k);            /* cross-plane east */
            link(me, ((p + P - 1) % P) * S + k);        /* cross-plane west */
        }
    }

    cdm = make_cdm(&g_sat[0], "1993-036BX", 140.0);
    printf("   T-6h  shared CDM: dead fragment 1993-036BX vs P0S00, miss "
           "140 m\n");
    if (!decide_and_announce(&g_sat[0], &cdm, T_TCA_MS - 21600000ll, &d, &nt,
                             wire))
    {
        printf("   unexpected: no give-way decision\n");
        return;
    }
    printf("         P0S00 decides alone: GIVE WAY (debris cannot be "
           "verified)\n");
    printf("         burn %.3f m/s at TCA-45 min -> miss %.0f m; return "
           "%.3f m/s over 3 orbits\n", d.plan.dv_mag,
           d.miss_m + d.plan.displacement_m, d.ret.total_dv);
    printf("         MANEUVER NOTICE (%u bytes) leaves P0S00 over its "
           "4 inter-satellite links:\n\n", SB_NOTICE_BYTES);
    (void)ripple(n, 0, wire, true);
    {
        /* Evaluate BEFORE printing: argument evaluation order is unspecified
         * and dig would otherwise be read before ledgers_agree() fills it. */
        const bool same = ledgers_agree(n, 0, &dig);
        printf("\n   flock ledgers: %s (digest %016llX on every satellite)\n",
               same ? "ALL 71 IDENTICAL" : "DIVERGED [BUG]",
               (unsigned long long)dig);
    }

    for (i = 1; i < n; ++i)
    {
        const SbFlockCheck c = sb_flock_check(&nt, g_sat[i].tca_state,
                                              T_TCA_MS);
        conflicts += c.conflict ? 1 : 0;
        if (c.min_sep_m < closest)
        {
            closest = c.min_sep_m;
            closest_i = i;
        }
    }
    printf("   every satellite checks the mover's new path against itself:\n");
    printf("   %d conflicts; closest neighbour %s stays %.0f km away -> "
           "nobody else moves\n", conflicts, g_sat[closest_i].name,
           closest / 1000.0);
}

/* Screen one candidate schedule for the cluster: the give-way craft burns
 * with burn lead `lead_ms`; either alone (unison = false) or with every
 * member flying the same Hill-frame burns at the same instants (unison =
 * true). Checks every pair of members and every member vs the fragment over
 * the whole maneuver window. Returns the number of conflicts. */
typedef struct {
    int    conflicts;
    double total_dv;
    double worst_pair;
    double worst_threat;
    SbNotice notice[MAX_SATS];
    SbDecision d;
} Candidate;

static void screen_candidate(int n, int mover, const SbCdm *cdm,
                             int64_t lead_ms, bool unison, Candidate *c)
{
    SbFswConfig cfg;
    OrbitConjunction oc;
    int64_t tca;
    int i;
    int j;
    double tau;
    const CraftState threat = cdm->obj[1].state_tca;
    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.self_id = g_sat[mover].id;
    cfg.self_tier = TIER_OPERATIONAL;
    cfg.self_can_maneuver = true;
    cfg.operating_mode = SB_MODE_AUTONOMOUS;
    cfg.burn_lead_ms = lead_ms;
    cfg.cmd_latency_ms = 60000;
    (void)memset(c, 0, sizeof(*c));
    c->worst_pair = 1.0e300;
    c->worst_threat = 1.0e300;
    (void)sb_fsw_decide(&cfg, cdm, NULL, T_TCA_MS - 21600000ll, &c->d);
    (void)sb_cdm_screen(cdm, &oc, &tca);
    tau = (double)(c->d.tca_epoch_ms - c->d.burn_epoch_ms) / 1000.0;
    for (i = 0; i < n; ++i)
    {
        const bool moves = unison || (i == mover);
        CraftState at_burn = sb_propagate(g_sat[i].tca_state, -tau);
        const Mat3 basis = sb_hill_basis(at_burn);
        SbNotice *nt = &c->notice[i];
        sb_notice_from_plan(&g_sat[mover].id, &cdm->obj[1].id, 1u,
                            c->d.conjunction_id, c->d.tca_epoch_ms,
                            c->d.burn_epoch_ms, oc.a_tca, &c->d.plan,
                            &c->d.ret, nt);
        nt->origin = g_sat[i].id;
        if (moves)
        {
            nt->dv_eci = sb_mat3_tmul(&basis, c->d.plan.dv_hill);
            at_burn.velocity = sb_vadd(at_burn.velocity, nt->dv_eci);
            c->total_dv += c->d.plan.dv_mag + c->d.ret.total_dv;
        }
        else
        {
            const Vec3 z = {0.0, 0.0, 0.0};
            nt->dv_eci = z;
            nt->mirror_dv_hill = z;
            nt->final_dv_hill = z;
        }
        nt->post_burn = sb_canonicalize(at_burn);
    }
    for (i = 0; i < n; ++i)
    {
        const SbFlockCheck ct = sb_flock_check(&c->notice[i], threat,
                                               T_TCA_MS);
        c->conflicts += ct.conflict ? 1 : 0;
        if (ct.min_sep_m < c->worst_threat)
        {
            c->worst_threat = ct.min_sep_m;
        }
        for (j = i + 1; j < n; ++j)
        {
            const SbFlockCheck cp = sb_flock_check_pair(
                &c->notice[i], &c->notice[j], g_sat[j].tca_state, T_TCA_MS);
            c->conflicts += cp.conflict ? 1 : 0;
            if (cp.min_sep_m < c->worst_pair)
            {
                c->worst_pair = cp.min_sep_m;
            }
        }
    }
}

static void act2(void)
{
    const int n = 8;
    const double r = SB_R_EARTH_M + 600.0e3;
    const double spacing_m = 2000.0;
    const double inc = 43.0 * SB_PI / 180.0;
    int i;
    SbDecision d;
    SbNotice nt;
    uint8_t wire[SB_NOTICE_BYTES];
    SbCdm cdm;
    uint64_t dig = 0u;
    int n_alert = 0;
    const int mover = 3;
    static Candidate cand[2][6];
    static const int64_t leads_s[6] = {2700, 3600, 4500, 5400, 6300, 7200};
    int best_k = -1;
    int best_u = 0;

    printf("\n Act II - a tight formation: 8-craft cluster, 2 km apart\n\n");
    for (i = 0; i < n; ++i)
    {
        Sat *s = &g_sat[i];
        char id[16];
        (void)memset(s, 0, sizeof(*s));
        (void)snprintf(id, sizeof(id), "2026-700%c", (char)('A' + i));
        (void)snprintf(s->name, sizeof(s->name), "DC-%d", i + 1);
        s->id = sb_make_object_id(id);
        s->tca_state = circular(r, inc, 0.3, spacing_m * (double)i / r);
        sb_flock_init(&s->ledger, s->id);
    }
    for (i = 0; i < n; ++i)
    {
        link(i, (i + 1) % n);                        /* ring of laser links */
        link(i, (i + n - 1) % n);
    }
    cdm = make_cdm(&g_sat[mover], "2009-005CQ", 60.0);
    printf("   shared CDM: fragment 2009-005CQ vs DC-4, miss 60 m\n");
    if (!decide_and_announce(&g_sat[mover], &cdm, T_TCA_MS - 21600000ll, &d,
                             &nt, wire))
    {
        printf("   unexpected: no give-way decision\n");
        return;
    }
    printf("   DC-4 decides GIVE WAY and announces its plan: burn %.3f m/s; "
           "its drift\n   and return sweep %.1f km along-track — through its "
           "neighbours' slots.\n\n", d.plan.dv_mag,
           6.0 * SB_PI * fabs(d.plan.dv_hill.y) /
               sb_mean_motion(g_sat[mover].tca_state) / 1000.0);
    (void)ripple(n, mover, wire, true);
    {
        const bool same = ledgers_agree(n, mover, &dig);
        printf("\n   flock ledgers: %s\n", same ? "all identical" : "DIVERGED [BUG]");
    }
    printf("   each member checks DC-4's announced path against itself:\n");
    for (i = 0; i < n; ++i)
    {
        SbFlockCheck c;
        if (i == mover)
        {
            continue;
        }
        c = sb_flock_check(&nt, g_sat[i].tca_state, T_TCA_MS);
        printf("     %-5s closest %6.0f m at burn+%5.0f s  %s\n",
               g_sat[i].name, c.min_sep_m,
               (double)(c.at_epoch_ms - nt.burn_epoch_ms) / 1000.0,
               c.conflict ? "<- FLOCK ALERT" : "clear");
        n_alert += c.conflict ? 1 : 0;
    }
    printf("\n   %d neighbours raise FLOCK ALERT, and all 8 agree on which: "
           "they hold the\n   same ledger. The alert never commands a burn. "
           "But since every member\n   holds the same CDM and ledger, the "
           "cluster can screen candidate plans\n   against the WHOLE flock "
           "and the fragment, and all reach the same choice:\n\n", n_alert);
    printf("     burn lead   DC-4 alone             whole cluster in unison\n");
    for (i = 0; i < 6; ++i)
    {
        int u;
        for (u = 0; u < 2; ++u)
        {
            screen_candidate(n, mover, &cdm, leads_s[i] * 1000, u != 0,
                             &cand[u][i]);
        }
        printf("     %5lld s    %d conflict%s %5.2f m/s    %d conflict%s "
               "%5.2f m/s\n", (long long)leads_s[i],
               cand[0][i].conflicts, (cand[0][i].conflicts == 1) ? " " : "s",
               cand[0][i].total_dv,
               cand[1][i].conflicts, (cand[1][i].conflicts == 1) ? " " : "s",
               cand[1][i].total_dv);
        for (u = 0; u < 2; ++u)
        {
            if ((cand[u][i].conflicts == 0) &&
                ((best_k < 0) ||
                 (cand[u][i].total_dv < cand[best_u][best_k].total_dv)))
            {
                best_k = i;
                best_u = u;
            }
        }
    }
    if (best_k < 0)
    {
        printf("\n   no candidate is clear: ESCALATE to operators (SPEC "
               "§7.5)\n");
        return;
    }
    {
        const Candidate *c = &cand[best_u][best_k];
        uint8_t w2[SB_NOTICE_BYTES];
        SbNotice upd = c->notice[mover];
        upd.seq = 2u;
        (void)sb_notice_encode(&upd, w2);
        printf("\n   chosen: burn lead %lld s, %s — cheapest plan with zero "
               "conflicts\n   (closest pair %.0f m, closest to the fragment "
               "%.0f m, %.2f m/s total).\n",
               (long long)leads_s[best_k],
               best_u ? "the cluster turns AS ONE" : "DC-4 alone",
               c->worst_pair, c->worst_threat, c->total_dv);
        printf("   DC-4 re-announces (seq 2); the update ripples through the "
               "ring:\n\n");
        (void)ripple(n, mover, w2, true);
        {
            const bool same = ledgers_agree(n, mover, &dig);
            printf("\n   ledgers: %s — the flock dodged the fragment without "
                   "a single member\n   passing within %0.f m of anything.\n",
                   same ? "identical again" : "DIVERGED [BUG]", SB_R_SAFE_M);
        }
    }
}

int main(void)
{
    printf("=============================================================\n");
    printf(" STARBOARD - the murmuration (Layer 4: the flock)\n");
    printf("=============================================================\n");
    act1();
    act2();
    printf("\n=============================================================\n");
    printf(" One craft moved; the whole flock knew, checked, and agreed.\n");
    printf("=============================================================\n");
    return 0;
}
