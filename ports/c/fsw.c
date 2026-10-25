// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// fsw.c — the flight integration layer (starboard_fsw.h, docs/FLIGHT.md).

#include "starboard_fsw.h"
#include "starboard_registry.h"
#include <string.h>

/* ======================================================================== */
/* Built-in self-test                                                       */
/* ======================================================================== */

static double d_of(uint64_t u)
{
    double d;
    (void)memcpy(&d, &u, sizeof(d));
    return d;
}

static uint64_t u_of(double d)
{
    uint64_t u;
    (void)memcpy(&u, &d, sizeof(u));
    return u;
}

/* x87-style excess precision / double rounding makes a conformant binary64
 * build impossible (SPEC §3.5). Two probes: (1) 1 + 2^-53 + 2^-78 rounds to
 * 1 + 2^-52 in binary64, but to 1.0 when first rounded to a 64-bit
 * significand; (2) excess exponent range lets 1e308*10/10 survive. */
static bool no_excess_precision(void)
{
    volatile double one = 1.0;
    volatile double tiny = d_of(0x3CA0000000000000ULL) +   /* 2^-53 */
                           d_of(0x3B10000000000000ULL);    /* 2^-78 */
    volatile double big = 1.0e308;
    volatile double ten = 10.0;
    volatile double sum = one + tiny;
    volatile double prod = big * ten;
    volatile double back = prod / ten;
    return (u_of(sum) == 0x3FF0000000000001ULL) && (back != big);
}

/* The canonical constants, by bit pattern, CRC-32'd at boot: catches a
 * corrupted .rodata (an upset in non-EDAC memory, a bad image). */
static uint32_t constants_crc(void)
{
    const double k[] = {
        SB_Q_POS_M, SB_Q_VEL_MPS, SB_Q_COV_M2, SB_Q_TIME_S, SB_MU_EARTH_M3S2,
        SB_J2_EARTH, SB_R_EARTH_M, SB_PI, SB_R_SCREEN_M, SB_R_SAFE_M,
        SB_PC_ACTIONABLE, SB_DEFAULT_HBR_M, SB_VAR_FLOOR_M2, SB_DEFAULT_VAR_M2,
        SB_PROP_STEP_S, SB_SCREEN_ARC_MAX_S, SB_SCREEN_STEP_S,
        SB_OVERTAKE_COS, SB_EPS_SPEED_MPS, SB_EPS_LATERAL_M, SB_EPS_BEARING,
        SB_MANEUVER_MARGIN, SB_MIN_LEAD_S, SB_CDM_OMEGA_EARTH_RADS,
        SB_CDM_BACKPROP_S, SB_PROP_DT_MAX_S
    };
    uint8_t buf[sizeof(k) + 8U];
    uint32_t i;
    const uint32_t ints[2] = {SB_RULESET_ID, SB_TCA_BUCKET_MS};
    for (i = 0U; i < (uint32_t)(sizeof(k) / sizeof(k[0])); ++i)
    {
        const uint64_t u = u_of(k[i]);
        uint32_t b;
        for (b = 0U; b < 8U; ++b)
        {
            buf[(i * 8U) + b] = (uint8_t)(u >> (8U * b));
        }
    }
    for (i = 0U; i < 2U; ++i)
    {
        uint32_t b;
        for (b = 0U; b < 4U; ++b)
        {
            buf[sizeof(k) + (i * 4U) + b] = (uint8_t)(ints[i] >> (8U * b));
        }
    }
    return sb_crc32(buf, (uint32_t)sizeof(buf));
}

/* Expected CRC of the constant table above (little-endian bit patterns).
 * Regenerate with `sbctl selftest --print-constants-crc` after a NORMATIVE
 * change — which is a RULESET_ID change anyway. */
#define SB_CONSTANTS_CRC (0x2432E8FBU)

/* KAT geometry: the SB-013 crossing pair, canonical states by bit pattern
 * (no libm needed to rebuild them on board). */
static void kat_pair(CraftState *a, CraftState *b)
{
    a->position.x = d_of(0x415A6F9400000000ULL);
    a->position.y = 0.0;
    a->position.z = 0.0;
    a->velocity.x = d_of(0x8000000000000000ULL);
    a->velocity.y = d_of(0x40BDA011A4400000ULL);
    a->velocity.z = 0.0;
    b->position.x = d_of(0x4155DCCE38E00000ULL);
    b->position.y = d_of(0x412E73EC18000000ULL);
    b->position.z = d_of(0xC14CB28530600000ULL);
    b->velocity.x = d_of(0x409113465B800000ULL);
    b->velocity.y = d_of(0x40BA114BFFB00000ULL);
    b->velocity.z = d_of(0x40AACA47EE200000ULL);
}

static const char SB_KAT_CDM[] =
    "CCSDS_CDM_VERS = 1.0\n"
    "MESSAGE_ID = SB-KAT\n"
    "ORIGINATOR = STARBOARD\n"
    "TCA = 2026-01-01T00:00:00.000\n"
    "MISS_DISTANCE = 100.0 [m]\n"
    "OBJECT = OBJECT1\n"
    "OBJECT_DESIGNATOR = 1\n"
    "INTERNATIONAL_DESIGNATOR = 2026-001A\n"
    "MANEUVERABLE = YES\n"
    "REF_FRAME = ITRF\n"
    "X = 6930.000000 [km]\nY = 0.0 [km]\nZ = 0.0 [km]\n"
    "X_DOT = 0.0 [km/s]\nY_DOT = 7.084000000 [km/s]\nZ_DOT = 0.0 [km/s]\n"
    "CR_R = 1.0E+02\nCT_R = 0.0\nCT_T = 4.0E+04\n"
    "CN_R = 0.0\nCN_T = 0.0\nCN_N = 2.5E+01\n"
    "OBJECT = OBJECT2\n"
    "OBJECT_DESIGNATOR = 2\n"
    "INTERNATIONAL_DESIGNATOR = 2026-044B\n"
    "MANEUVERABLE = NO\n"
    "REF_FRAME = ITRF\n"
    "X = 6930.100000 [km]\nY = 0.0 [km]\nZ = 0.0 [km]\n"
    "X_DOT = 0.0 [km/s]\nY_DOT = 0.0 [km/s]\nZ_DOT = 7.084000000 [km/s]\n"
    "CR_R = 1.0E+02\nCT_R = 0.0\nCT_T = 4.0E+04\n"
    "CN_R = 0.0\nCN_T = 0.0\nCN_N = 2.5E+01\n";

/* Expected dense transcendental sweep fold (== v1 transcript). */
#define SB_TRANSCENDENTAL_FOLD (0x968AE374ED4E17A0ULL)

/* Fold of the bound KAT CDM (both states + covariances + epoch). */
#define SB_KAT_CDM_FOLD (0x17D01B3CC2113E41ULL)

static uint64_t fold_d(uint64_t h, double d)
{
    /* Fold the IEEE bit pattern in LITTLE-ENDIAN byte order, explicitly:
     * hashing raw memory would make the known answer depend on host byte
     * order (it did: the first cross-architecture run flagged every
     * big-endian target). */
    const uint64_t u = u_of(d);
    uint32_t i;
    for (i = 0U; i < 8U; ++i)
    {
        h ^= (u >> (8U * i)) & 0xFFU;
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t sb_fsw_kat_cdm_fold(void)
{
    SbCdm cdm;
    uint64_t h = 14695981039346656037ULL;
    int i;
    if (sb_cdm_parse(SB_KAT_CDM, (uint32_t)(sizeof(SB_KAT_CDM) - 1U), &cdm,
                     NULL) != SB_CDM_OK)
    {
        return 0U;
    }
    for (i = 0; i < 2; ++i)
    {
        const CraftState *st = &cdm.obj[i].state_tca;
        const Sym33 *c = &cdm.obj[i].covariance;
        h = fold_d(h, st->position.x);
        h = fold_d(h, st->position.y);
        h = fold_d(h, st->position.z);
        h = fold_d(h, st->velocity.x);
        h = fold_d(h, st->velocity.y);
        h = fold_d(h, st->velocity.z);
        h = fold_d(h, c->xx);
        h = fold_d(h, c->xy);
        h = fold_d(h, c->xz);
        h = fold_d(h, c->yy);
        h = fold_d(h, c->yz);
        h = fold_d(h, c->zz);
    }
    h = fold_d(h, (double)cdm.tca_gps_ms);
    return h;
}

/* The transcript's dense transcendental sweep (v1 line "transcendental"):
 * 8193 canon_sincos + 4097 canon_exp evaluations folded. Catches a platform
 * whose arithmetic misrounds only in corners the three anchors miss. */
uint64_t sb_fsw_transcendental_fold(void)
{
    uint64_t h = 14695981039346656037ULL;
    int i;
    for (i = -4096; i <= 4096; ++i)
    {
        const double x = 0.017 * (double)i;
        double sc;
        double cc;
        sb_canon_sincos(x, &sc, &cc);
        h = fold_d(h, sc);
        h = fold_d(h, cc);
        if (x <= 0.0)
        {
            h = fold_d(h, sb_canon_exp(x));
        }
    }
    return h;
}

/* IEEE-754 rounding probes: additions/subtractions whose exact results sit
 * just off a rounding boundary, at the exponent gaps where software
 * floating-point is most often wrong (31..34). The first is the case the
 * cross-architecture campaign found misrounded under ARM soft-float
 * (1 + -0x1.72e0936a3d520p-33). Expected values are the correctly rounded
 * results, verified with exact rational arithmetic. */
static bool ieee_rounding_ok(void)
{
    static const uint64_t probe[6][3] = {
        {0x3FF0000000000000ULL, 0xBDE72E0936A3D520ULL, 0x3FEFFFFFFFE8D1F7ULL},
        {0x3FF0000000000000ULL, 0xBDE00D0D4C313944ULL, 0x3FEFFFFFFFEFF2F3ULL},
        {0x3FF0000000000000ULL, 0xBDF00D0D4C313944ULL, 0x3FEFFFFFFFDFE5E5ULL},
        {0x3FF0000000000000ULL, 0xBDD00D0D4C313944ULL, 0x3FEFFFFFFFF7F979ULL},
        {0x3FF0000000000000ULL, 0x3DE00D0D4C313944ULL, 0x3FF0000000080687ULL},
        {0x3FF8000000000001ULL, 0xBE000D0D4C313944ULL, 0x3FF7FFFFFFDFE5E6ULL}
    };
    uint32_t i;
    for (i = 0U; i < 6U; ++i)
    {
        volatile double a = d_of(probe[i][0]);
        volatile double b = d_of(probe[i][1]);
        volatile double r = a + b;
        if (u_of(r) != probe[i][2])
        {
            return false;
        }
    }
    return true;
}

uint32_t sb_fsw_constants_crc(void)
{
    return constants_crc();
}

static uint32_t run_selftest(void)
{
    uint32_t failed = 0U;
    CraftState ka;
    CraftState kb;
    OrbitConjunction oc;

    if (!sb_arithmetic_profile_ok())
    {
        failed |= SB_BIT_ARITH_FMA;
    }
    if (!no_excess_precision())
    {
        failed |= SB_BIT_ARITH_EXCESS;
    }
    if (!ieee_rounding_ok())
    {
        failed |= SB_BIT_ARITH_ROUND;
    }
    if ((u_of(sb_canon_exp(-0.5)) != 0x3FE368B2FC6F960AULL) ||
        (u_of(sb_canon_sin(1.0)) != 0x3FEAED548F090CEEULL) ||
        (u_of(sb_canon_cos(1.0)) != 0x3FE14A280FB5068CULL) ||
        (sb_fsw_transcendental_fold() != SB_TRANSCENDENTAL_FOLD))
    {
        failed |= SB_BIT_TRANSCEND;
    }

    kat_pair(&ka, &kb);
    (void)memset(&oc, 0, sizeof(oc));
    if ((!sb_screen_orbits(ka, kb, sb_sym33_diagonal(9.0e4),
                           sb_sym33_diagonal(9.0e4), 20.0,
                           SB_SCREEN_ARC_MAX_S, &oc)) ||
        (u_of(oc.conj.t_ca) != 0x4089000000000000ULL) ||
        (u_of(oc.conj.miss_distance) != 0x4062BFF989262397ULL) ||
        (u_of(oc.conj.pc) != 0x3F5117B4A88B614CULL))
    {
        failed |= SB_BIT_SEARCH;
    }
    else
    {
        CraftDescriptor a;
        CraftDescriptor b;
        GiveWayVerdict v;
        GiveWayVerdict w;
        GiveWayPlan p;
        const ObjectId gw = sb_make_object_id("2026-001A");
        (void)memset(&a, 0, sizeof(a));
        (void)memset(&b, 0, sizeof(b));
        a.object_id = sb_make_object_id("2026-001A");
        b.object_id = sb_make_object_id("2026-044B");
        a.tier = TIER_OPERATIONAL;
        b.tier = TIER_OPERATIONAL;
        a.can_maneuver = true;
        b.can_maneuver = true;
        a.trust = TRUST_VERIFIED;
        b.trust = TRUST_VERIFIED;
        a.state = oc.a_tca;
        b.state = oc.b_tca;
        v = sb_evaluate(&a, &b, 1401000000000ULL);
        w = sb_evaluate(&b, &a, 1401000000000ULL);
        if ((v.deciding_rule != RULE_STARBOARD) ||
            (!sb_id_equal(&v.give_way_id, &gw)) ||
            (!sb_id_equal(&w.give_way_id, &gw)) ||
            (v.conjunction_id != w.conjunction_id) ||
            (v.self_role != ROLE_GIVE_WAY) || (w.self_role != ROLE_STAND_ON))
        {
            failed |= SB_BIT_VERDICT;
        }
        p = sb_plan_give_way(ka, oc.a_tca, &oc.conj, true, SB_R_SAFE_M);
        /* Exact-physics check rather than pinned bits: the plan must reach
         * kappa*R_SAFE in CW (displacement + miss) to 1e-6 relative. */
        if ((!p.required) ||
            (((p.displacement_m + oc.conj.miss_distance) -
              (SB_MANEUVER_MARGIN * SB_R_SAFE_M)) > 1.0e-3) ||
            (((p.displacement_m + oc.conj.miss_distance) -
              (SB_MANEUVER_MARGIN * SB_R_SAFE_M)) < -1.0e-3))
        {
            failed |= SB_BIT_MANEUVER;
        }
    }

    {
        static const uint8_t abc_digest_head[8] = {
            0xDDU, 0xAFU, 0x35U, 0xA1U, 0x93U, 0x61U, 0x7AU, 0xBAU};
        uint8_t dig[64];
        const uint8_t abc[3] = {0x61U, 0x62U, 0x63U};
        sb_sha512(abc, 3U, dig);
        if (memcmp(dig, abc_digest_head, 8U) != 0)
        {
            failed |= SB_BIT_SHA512;
        }
    }
    {
        /* RFC 8032 §7.1 TEST 1 (empty message), and a 1-bit corruption. */
        static const uint8_t pk[32] = {
            0xd7U, 0x5aU, 0x98U, 0x01U, 0x82U, 0xb1U, 0x0aU, 0xb7U, 0xd5U,
            0x4bU, 0xfeU, 0xd3U, 0xc9U, 0x64U, 0x07U, 0x3aU, 0x0eU, 0xe1U,
            0x72U, 0xf3U, 0xdaU, 0xa6U, 0x23U, 0x25U, 0xafU, 0x02U, 0x1aU,
            0x68U, 0xf7U, 0x07U, 0x51U, 0x1aU};
        static const uint8_t sig[64] = {
            0xe5U, 0x56U, 0x43U, 0x00U, 0xc3U, 0x60U, 0xacU, 0x72U, 0x90U,
            0x86U, 0xe2U, 0xccU, 0x80U, 0x6eU, 0x82U, 0x8aU, 0x84U, 0x87U,
            0x7fU, 0x1eU, 0xb8U, 0xe5U, 0xd9U, 0x74U, 0xd8U, 0x73U, 0xe0U,
            0x65U, 0x22U, 0x49U, 0x01U, 0x55U, 0x5fU, 0xb8U, 0x82U, 0x15U,
            0x90U, 0xa3U, 0x3bU, 0xacU, 0xc6U, 0x1eU, 0x39U, 0x70U, 0x1cU,
            0xf9U, 0xb4U, 0x6bU, 0xd2U, 0x5bU, 0xf5U, 0xf0U, 0x59U, 0x5bU,
            0xbeU, 0x24U, 0x65U, 0x51U, 0x41U, 0x43U, 0x8eU, 0x7aU, 0x10U,
            0x0bU};
        uint8_t bad[64];
        (void)memcpy(bad, sig, 64U);
        bad[10] ^= 0x01U;
        if ((!sb_ed25519_verify(sig, NULL, 0U, pk)) ||
            sb_ed25519_verify(bad, NULL, 0U, pk))
        {
            failed |= SB_BIT_ED25519;
        }
    }
    if ((SB_KAT_CDM_FOLD != 0ULL) && (sb_fsw_kat_cdm_fold() != SB_KAT_CDM_FOLD))
    {
        failed |= SB_BIT_CDM;
    }
    if ((SB_CONSTANTS_CRC != 0U) && (constants_crc() != SB_CONSTANTS_CRC))
    {
        failed |= SB_BIT_CONSTANTS;
    }
    return failed;
}

/* ======================================================================== */
/* Decisions                                                                */
/* ======================================================================== */

SbFswStatus sb_fsw_decide(const SbFswConfig *cfg, const SbCdm *cdm,
                          const SbCounterpart *other, int64_t now_gps_ms,
                          SbDecision *out)
{
    OrbitConjunction oc;
    int64_t tca_ms = 0;
    uint32_t si;
    uint32_t oi;
    CraftDescriptor self_d;
    CraftDescriptor other_d;
    GiveWayVerdict v;

    (void)memset(out, 0, sizeof(*out));
    if (sb_id_equal(&cdm->obj[0].id, &cfg->self_id))
    {
        si = 0U;
    }
    else if (sb_id_equal(&cdm->obj[1].id, &cfg->self_id))
    {
        si = 1U;
    }
    else
    {
        return SB_FSW_ERR_NOT_INVOLVED;
    }
    oi = 1U - si;
    out->self_index = (uint8_t)si;

    out->found = sb_cdm_screen(cdm, &oc, &tca_ms) ? 1U : 0U;
    out->tca_epoch_ms = tca_ms;
    out->miss_m = oc.conj.miss_distance;
    out->pc = oc.conj.pc;
    if ((out->found == 0U) || (!sb_actionable(&oc.conj)))
    {
        out->role = SB_ROLE_NONE;
        return SB_FSW_OK;
    }
    out->actionable = 1U;

    (void)memset(&self_d, 0, sizeof(self_d));
    (void)memset(&other_d, 0, sizeof(other_d));
    self_d.object_id = cfg->self_id;
    self_d.tier = cfg->self_tier;
    self_d.can_maneuver = cfg->self_can_maneuver;
    self_d.trust = TRUST_VERIFIED;               /* a craft knows itself */
    self_d.hard_body_radius_m = SB_DEFAULT_HBR_M;
    self_d.state = (si == 0U) ? oc.a_tca : oc.b_tca;
    self_d.covariance = cdm->obj[si].covariance;
    other_d.object_id = cdm->obj[oi].id;
    if ((other != NULL) && other->used)
    {
        other_d.trust = other->trust;
        other_d.tier = other->tier;
        other_d.can_maneuver = other->can_maneuver;
    }
    else
    {
        /* No registry outcome: the CDM's MANEUVERABLE claim is at most
         * ASSERTED (Table 8-1 then treats the craft as unable). */
        other_d.trust = TRUST_UNKNOWN;
        other_d.tier = TIER_OPERATIONAL;
        other_d.can_maneuver = (cdm->obj[oi].maneuverable == SB_MAN_YES);
    }
    other_d.hard_body_radius_m = SB_DEFAULT_HBR_M;
    other_d.state = (si == 0U) ? oc.b_tca : oc.a_tca;
    other_d.covariance = cdm->obj[oi].covariance;

    v = sb_evaluate(&self_d, &other_d, (uint64_t)tca_ms);
    out->rule = (uint8_t)v.deciding_rule;
    out->advisory = v.advisory ? 1U : 0U;
    out->conjunction_id = v.conjunction_id;
    out->role = (v.self_role == ROLE_GIVE_WAY) ? SB_ROLE_GIVE_WAY
                                               : SB_ROLE_STAND_ON;

    if ((out->role == SB_ROLE_GIVE_WAY) && cfg->self_can_maneuver)
    {
        int64_t burn = tca_ms - cfg->burn_lead_ms;
        const int64_t earliest = now_gps_ms + cfg->cmd_latency_ms;
        double tau;
        Conjunction mine;
        CraftState self_now;
        if (burn < earliest)
        {
            burn = earliest;
            out->late = 1U;
        }
        tau = (double)(tca_ms - burn) / 1000.0;
        if (tau < SB_MIN_LEAD_S)
        {
            tau = SB_MIN_LEAD_S;
            out->late = 1U;
        }
        if (tau > SB_PROP_DT_MAX_S)
        {
            tau = SB_PROP_DT_MAX_S;
        }
        out->burn_epoch_ms = tca_ms - (int64_t)(tau * 1000.0);
        mine.detected = true;
        mine.t_ca = tau;
        mine.miss_vector = sb_vsub(other_d.state.position,
                                   self_d.state.position);
        mine.rel_velocity = sb_vsub(other_d.state.velocity,
                                    self_d.state.velocity);
        mine.miss_distance = sb_vnorm(mine.miss_vector);
        mine.pc = oc.conj.pc;
        /* Burn-time state by canonical back-propagation from the shared TCA
         * state: the plan is a pure function of shared data, so the ground
         * (and every neighbour holding the CDM) reproduces it exactly. */
        self_now = sb_propagate(self_d.state, -tau);
        out->plan = sb_plan_give_way(
            self_now, self_d.state, &mine,
            sb_id_compare(&self_d.object_id, &other_d.object_id) <= 0,
            SB_R_SAFE_M);
        if (out->plan.required)
        {
            out->ret = sb_plan_mirror_return(out->plan.dv_hill,
                                             sb_mean_motion(self_now),
                                             SB_RETURN_PHASING_ORBITS);
        }
        else
        {
            out->burn_epoch_ms = 0;
        }
    }
    return SB_FSW_OK;
}

/* Canonical serialization. Redundant runs and stored inputs are compared /
 * CRC'd over THESE bytes, never over raw struct memory: struct assignment
 * does not preserve padding bytes, so a raw memcmp reports phantom
 * mismatches (found by the fault-injection tests — it would have raised
 * false SEU alarms on orbit). */
typedef struct {
    uint8_t  b[512];
    uint32_t n;
} Ser;

static void ser_bytes(Ser *s, const void *p, uint32_t n)
{
    const uint8_t *q = (const uint8_t *)p;
    uint32_t i;
    for (i = 0U; (i < n) && (s->n < (uint32_t)sizeof(s->b)); ++i)
    {
        s->b[s->n] = q[i];
        ++s->n;
    }
}

static void ser_u64(Ser *s, uint64_t v)
{
    uint8_t t[8];
    uint32_t i;
    for (i = 0U; i < 8U; ++i)
    {
        t[i] = (uint8_t)(v >> (8U * i));
    }
    ser_bytes(s, t, 8U);
}

static void ser_d(Ser *s, double d)
{
    ser_u64(s, u_of(d));
}

static void ser_v(Ser *s, Vec3 v)
{
    ser_d(s, v.x);
    ser_d(s, v.y);
    ser_d(s, v.z);
}

static void ser_decision(Ser *s, const SbDecision *d)
{
    const uint8_t f[8] = {d->found, d->actionable, d->role, d->rule,
                          d->advisory, d->late, d->self_index,
                          (uint8_t)(d->plan.required ? 1U : 0U)};
    s->n = 0U;
    ser_bytes(s, f, 8U);
    ser_u64(s, d->conjunction_id);
    ser_u64(s, (uint64_t)d->tca_epoch_ms);
    ser_u64(s, (uint64_t)d->burn_epoch_ms);
    ser_d(s, d->miss_m);
    ser_d(s, d->pc);
    ser_v(s, d->plan.dv_eci);
    ser_v(s, d->plan.dv_hill);
    ser_d(s, d->plan.dv_mag);
    ser_d(s, d->plan.displacement_m);
    ser_v(s, d->ret.mirror_dv_hill);
    ser_v(s, d->ret.final_dv_hill);
    ser_d(s, d->ret.wait_s);
    ser_d(s, d->ret.transfer_s);
    ser_d(s, d->ret.total_dv);
}

bool sb_fsw_decision_equal(const SbDecision *a, const SbDecision *b)
{
    Ser sa;
    Ser sb;
    ser_decision(&sa, a);
    ser_decision(&sb, b);
    return (sa.n == sb.n) && (memcmp(sa.b, sb.b, sa.n) == 0);
}

static void ser_cdm(Ser *s, const SbCdm *c)
{
    uint32_t i;
    s->n = 0U;
    ser_u64(s, (uint64_t)c->tca_gps_ms);
    ser_d(s, c->hbr_m);
    for (i = 0U; i < 2U; ++i)
    {
        const SbCdmObject *o = &c->obj[i];
        const uint8_t f[3] = {(uint8_t)o->frame, (uint8_t)o->maneuverable,
                              (uint8_t)o->object_type};
        ser_bytes(s, o->id.c, 12U);
        ser_bytes(s, f, 3U);
        ser_v(s, o->state_tca.position);
        ser_v(s, o->state_tca.velocity);
        ser_d(s, o->covariance.xx);
        ser_d(s, o->covariance.xy);
        ser_d(s, o->covariance.xz);
        ser_d(s, o->covariance.yy);
        ser_d(s, o->covariance.yz);
        ser_d(s, o->covariance.zz);
    }
}

#ifdef SB_FSW_FAULT_INJECTION
/* Test-only (never in a flight build): flip one bit of the result of the
 * Nth decision run, emulating a single-event upset in the datapath. */
extern uint32_t sb_fi_run;
extern uint32_t sb_fi_flip_mask;
uint32_t sb_fi_run = 0U;
uint32_t sb_fi_flip_mask = 0U;     /* bit k set: corrupt run k (1-based) */
static void fault_inject(SbDecision *d)
{
    ++sb_fi_run;
    if ((sb_fi_run < 32U) && (((sb_fi_flip_mask >> sb_fi_run) & 1U) != 0U))
    {
        uint8_t *b = (uint8_t *)&d->pc;
        b[sb_fi_run % 8U] ^= (uint8_t)(1U << (sb_fi_run % 7U));
    }
}
#define SB_FAULT_POINT(d) fault_inject(d)
#else
#define SB_FAULT_POINT(d) ((void)0)
#endif

/* Redundant evaluation: two runs compared bit-for-bit; on mismatch a third
 * run and a 2-of-3 vote. Because the decision is bit-deterministic, ANY
 * difference between runs is a hardware fault (an upset in a register, ALU
 * or cache line) — no tolerance threshold is needed or wanted. */
static SbFswStatus decide_redundant(SbFsw *ctx, const SbCdm *cdm,
                                    const SbCounterpart *other, int64_t now,
                                    SbDecision *out)
{
    SbDecision r1;
    SbDecision r2;
    SbDecision r3;
    SbFswStatus s1 = sb_fsw_decide(&ctx->cfg, cdm, other, now, &r1);
    SbFswStatus s2;
    SB_FAULT_POINT(&r1);
    s2 = sb_fsw_decide(&ctx->cfg, cdm, other, now, &r2);
    SB_FAULT_POINT(&r2);
    ctx->evaluations += 2U;
    if ((s1 == s2) && sb_fsw_decision_equal(&r1, &r2))
    {
        *out = r1;
        return s1;
    }
    {
        const SbFswStatus s3 = sb_fsw_decide(&ctx->cfg, cdm, other, now, &r3);
        SB_FAULT_POINT(&r3);
        ctx->evaluations += 1U;
        if ((s3 == s1) && sb_fsw_decision_equal(&r3, &r1))
        {
            ++ctx->seu_corrected;
            *out = r1;
            return s1;
        }
        if ((s3 == s2) && sb_fsw_decision_equal(&r3, &r2))
        {
            ++ctx->seu_corrected;
            *out = r2;
            return s2;
        }
    }
    ++ctx->seu_uncorrected;
    (void)memset(out, 0, sizeof(*out));
    return SB_FSW_ERR_FAULT;
}

/* ======================================================================== */
/* Context management                                                       */
/* ======================================================================== */

static void set_error(SbFsw *ctx, SbFswStatus st)
{
    ctx->last_error = (uint8_t)st;
    ctx->hk_pending = true;
}

uint32_t sb_fsw_selftest(SbFsw *ctx)
{
    ctx->selftest_failed = run_selftest();
    if (ctx->selftest_failed != 0U)
    {
        ctx->mode = SB_MODE_SAFE;
    }
    else if ((ctx->mode == SB_MODE_SAFE) || (ctx->mode == SB_MODE_BOOT))
    {
        ctx->mode = ctx->cfg.operating_mode;
    }
    else
    {
        /* stay in the commanded mode */
    }
    ctx->hk_pending = true;
    return ctx->selftest_failed;
}

SbFswStatus sb_fsw_init(SbFsw *ctx, const SbFswConfig *cfg)
{
    if ((ctx == NULL) || (cfg == NULL) ||
        ((cfg->operating_mode != SB_MODE_ADVISORY) &&
         (cfg->operating_mode != SB_MODE_AUTONOMOUS)) ||
        (cfg->burn_lead_ms < 0) || (cfg->cmd_latency_ms < 0) ||
        (cfg->apid_base > 0x7FCU))
    {
        return SB_FSW_ERR_ARG;
    }
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->mode = SB_MODE_BOOT;
    (void)sb_fsw_selftest(ctx);
    return (ctx->mode == SB_MODE_SAFE) ? SB_FSW_ERR_FAULT : SB_FSW_OK;
}

SbFswStatus sb_fsw_set_mode(SbFsw *ctx, SbMode mode)
{
    if ((mode != SB_MODE_SAFE) && (mode != SB_MODE_ADVISORY) &&
        (mode != SB_MODE_AUTONOMOUS))
    {
        return SB_FSW_ERR_ARG;
    }
    if ((ctx->mode == SB_MODE_SAFE) && (mode != SB_MODE_SAFE))
    {
        if (run_selftest() != 0U)
        {
            set_error(ctx, SB_FSW_ERR_MODE);
            return SB_FSW_ERR_MODE;
        }
        ctx->selftest_failed = 0U;
    }
    ctx->mode = mode;
    ctx->hk_pending = true;
    return SB_FSW_OK;
}

static const SbCounterpart *find_counterpart(const SbFsw *ctx, ObjectId id)
{
    uint32_t i;
    for (i = 0U; i < SB_FSW_MAX_COUNTERPARTS; ++i)
    {
        if (ctx->counterparts[i].used &&
            sb_id_equal(&ctx->counterparts[i].id, &id))
        {
            return &ctx->counterparts[i];
        }
    }
    return NULL;
}

static uint32_t cdm_crc(const SbCdm *c)
{
    Ser s;
    ser_cdm(&s, c);
    return sb_crc32(s.b, s.n);
}

static void queue_decision_tm(SbFsw *ctx, SbEncounter *e)
{
    e->pending_tm |= 0x01U;
    if ((ctx->mode == SB_MODE_AUTONOMOUS) &&
        (e->decision.role == SB_ROLE_GIVE_WAY) && e->decision.plan.required)
    {
        e->pending_tm |= 0x02U;
    }
}

/* Rolling CDMs: an update for the same counterpart whose TCA lies within
 * +-2 conjunction-id buckets of a tracked encounter replaces it (PROTOCOL
 * Appendix B hysteresis); otherwise a free slot is used. */
static int32_t find_slot(const SbFsw *ctx, const SbCdm *cdm, uint32_t oi)
{
    const int64_t window = 2 * (int64_t)SB_TCA_BUCKET_MS;
    int32_t free_slot = -1;
    uint32_t i;
    for (i = 0U; i < SB_FSW_MAX_ENCOUNTERS; ++i)
    {
        const SbEncounter *e = &ctx->enc[i];
        if (!e->used)
        {
            if (free_slot < 0)
            {
                free_slot = (int32_t)i;
            }
            continue;
        }
        {
            const uint32_t eo = 1U - (uint32_t)e->decision.self_index;
            const int64_t dt = e->cdm.tca_gps_ms - cdm->tca_gps_ms;
            if (sb_id_equal(&e->cdm.obj[eo].id, &cdm->obj[oi].id) &&
                (dt <= window) && (dt >= -window))
            {
                return (int32_t)i;
            }
        }
    }
    return free_slot;
}

SbFswStatus sb_fsw_ingest_cdm(SbFsw *ctx, const char *kvn, uint32_t len,
                              int64_t now_gps_ms, uint32_t *slot)
{
    SbCdm cdm;
    SbCdmStatus cs;
    SbDecision d;
    SbFswStatus st;
    uint32_t si;
    uint32_t oi;
    int32_t k;

    if ((ctx->mode != SB_MODE_ADVISORY) && (ctx->mode != SB_MODE_AUTONOMOUS))
    {
        set_error(ctx, SB_FSW_ERR_MODE);
        return SB_FSW_ERR_MODE;
    }
    cs = sb_cdm_parse(kvn, len, &cdm, NULL);
    if (cs != SB_CDM_OK)
    {
        ++ctx->cdm_rejected;
        ctx->last_cdm_error = (uint8_t)cs;
        set_error(ctx, SB_FSW_ERR_CDM);
        return SB_FSW_ERR_CDM;
    }
    if (sb_id_equal(&cdm.obj[0].id, &ctx->cfg.self_id))
    {
        si = 0U;
    }
    else if (sb_id_equal(&cdm.obj[1].id, &ctx->cfg.self_id))
    {
        si = 1U;
    }
    else
    {
        ++ctx->cdm_rejected;
        set_error(ctx, SB_FSW_ERR_NOT_INVOLVED);
        return SB_FSW_ERR_NOT_INVOLVED;
    }
    oi = 1U - si;
    if (cdm.tca_gps_ms < now_gps_ms)
    {
        ++ctx->cdm_rejected;
        set_error(ctx, SB_FSW_ERR_STALE);
        return SB_FSW_ERR_STALE;
    }
    st = decide_redundant(ctx, &cdm, find_counterpart(ctx, cdm.obj[oi].id),
                          now_gps_ms, &d);
    if (st != SB_FSW_OK)
    {
        set_error(ctx, st);
        return st;
    }
    k = find_slot(ctx, &cdm, oi);
    if (k < 0)
    {
        set_error(ctx, SB_FSW_ERR_TABLE_FULL);
        return SB_FSW_ERR_TABLE_FULL;
    }
    ctx->enc[k].used = true;
    ctx->enc[k].cdm = cdm;
    ctx->enc[k].input_crc = cdm_crc(&cdm);
    ctx->enc[k].decision = d;
    ctx->enc[k].pending_tm = 0U;
    queue_decision_tm(ctx, &ctx->enc[k]);
    if ((ctx->enc[k].pending_tm & 0x02U) != 0U)
    {
        ++ctx->maneuver_requests;
    }
    ++ctx->cdm_accepted;
    if (slot != NULL)
    {
        *slot = (uint32_t)k;
    }
    return SB_FSW_OK;
}

SbFswStatus sb_fsw_set_counterpart(SbFsw *ctx, ObjectId id, TrustLevel trust,
                                   Tier tier, bool can_maneuver,
                                   int64_t now_gps_ms)
{
    SbCounterpart *c = NULL;
    uint32_t i;
    SbFswStatus result = SB_FSW_OK;
    for (i = 0U; (i < SB_FSW_MAX_COUNTERPARTS) && (c == NULL); ++i)
    {
        if (ctx->counterparts[i].used &&
            sb_id_equal(&ctx->counterparts[i].id, &id))
        {
            c = &ctx->counterparts[i];
        }
    }
    for (i = 0U; (i < SB_FSW_MAX_COUNTERPARTS) && (c == NULL); ++i)
    {
        if (!ctx->counterparts[i].used)
        {
            c = &ctx->counterparts[i];
        }
    }
    if (c == NULL)
    {
        set_error(ctx, SB_FSW_ERR_TABLE_FULL);
        return SB_FSW_ERR_TABLE_FULL;
    }
    c->used = true;
    c->id = id;
    c->trust = trust;
    c->tier = tier;
    c->can_maneuver = can_maneuver;

    if ((ctx->mode != SB_MODE_ADVISORY) && (ctx->mode != SB_MODE_AUTONOMOUS))
    {
        return SB_FSW_OK;      /* recorded; decided once a mode allows it */
    }
    for (i = 0U; i < SB_FSW_MAX_ENCOUNTERS; ++i)
    {
        SbEncounter *e = &ctx->enc[i];
        SbDecision d;
        SbFswStatus st;
        uint32_t oi;
        if (!e->used)
        {
            continue;
        }
        oi = 1U - (uint32_t)e->decision.self_index;
        if (!sb_id_equal(&e->cdm.obj[oi].id, &id))
        {
            continue;
        }
        if (cdm_crc(&e->cdm) != e->input_crc)
        {
            /* The stored input was corrupted after ingest: drop it and
             * report — a fresh CDM is required. */
            ++ctx->input_corruptions;
            (void)memset(e, 0, sizeof(*e));
            result = SB_FSW_ERR_FAULT;
            continue;
        }
        st = decide_redundant(ctx, &e->cdm, c, now_gps_ms, &d);
        if (st != SB_FSW_OK)
        {
            result = st;
            continue;
        }
        if (!sb_fsw_decision_equal(&d, &e->decision))
        {
            e->decision = d;
            queue_decision_tm(ctx, e);
            if ((e->pending_tm & 0x02U) != 0U)
            {
                ++ctx->maneuver_requests;
            }
        }
    }
    if (result != SB_FSW_OK)
    {
        set_error(ctx, result);
    }
    return result;
}

void sb_fsw_expire(SbFsw *ctx, int64_t now_gps_ms, int64_t retain_ms)
{
    uint32_t i;
    for (i = 0U; i < SB_FSW_MAX_ENCOUNTERS; ++i)
    {
        if (ctx->enc[i].used &&
            ((ctx->enc[i].cdm.tca_gps_ms + retain_ms) < now_gps_ms))
        {
            (void)memset(&ctx->enc[i], 0, sizeof(ctx->enc[i]));
        }
    }
}

uint32_t sb_fsw_notice(SbFsw *ctx, uint32_t slot,
                       uint8_t out[SB_NOTICE_BYTES])
{
    const SbEncounter *e;
    OrbitConjunction oc;
    int64_t tca;
    SbNotice n;
    uint32_t si;
    if (slot >= SB_FSW_MAX_ENCOUNTERS)
    {
        return 0U;
    }
    e = &ctx->enc[slot];
    if ((!e->used) || (e->decision.role != SB_ROLE_GIVE_WAY) ||
        (!e->decision.plan.required))
    {
        return 0U;
    }
    si = (uint32_t)e->decision.self_index;
    (void)sb_cdm_screen(&e->cdm, &oc, &tca);
    ++ctx->notice_seq;
    sb_notice_from_plan(&ctx->cfg.self_id, &e->cdm.obj[1U - si].id,
                        ctx->notice_seq, e->decision.conjunction_id,
                        e->decision.tca_epoch_ms, e->decision.burn_epoch_ms,
                        (si == 0U) ? oc.a_tca : oc.b_tca, &e->decision.plan,
                        &e->decision.ret, &n);
    return sb_notice_encode(&n, out);
}

void sb_fsw_request_hk(SbFsw *ctx)
{
    ctx->hk_pending = true;
}

/* ======================================================================== */
/* Telemetry (big-endian payloads, docs/FLIGHT.md §6)                       */
/* ======================================================================== */

typedef struct {
    uint8_t *p;
    uint32_t n;
    uint32_t cap;
    bool     ok;
} Wr;

static void w_u8(Wr *w, uint8_t v)
{
    if (w->n < w->cap)
    {
        w->p[w->n] = v;
        ++w->n;
    }
    else
    {
        w->ok = false;
    }
}

static void w_u16(Wr *w, uint16_t v)
{
    w_u8(w, (uint8_t)(v >> 8));
    w_u8(w, (uint8_t)v);
}

static void w_u32(Wr *w, uint32_t v)
{
    w_u16(w, (uint16_t)(v >> 16));
    w_u16(w, (uint16_t)v);
}

static void w_u64(Wr *w, uint64_t v)
{
    w_u32(w, (uint32_t)(v >> 32));
    w_u32(w, (uint32_t)v);
}

static void w_f64(Wr *w, double d)
{
    w_u64(w, u_of(d));
}

static void w_vec(Wr *w, Vec3 v)
{
    w_f64(w, v.x);
    w_f64(w, v.y);
    w_f64(w, v.z);
}

static void w_id(Wr *w, const ObjectId *id)
{
    uint32_t i;
    for (i = 0U; i < 12U; ++i)
    {
        w_u8(w, (uint8_t)id->c[i]);
    }
}

static uint32_t emit(SbFsw *ctx, uint8_t *buf, uint32_t cap, uint16_t apid,
                     int64_t now, const Wr *payload)
{
    const uint32_t n = sb_ccsds_tm_packet(buf, cap, apid, ctx->tm_seq, now,
                                          payload->p, payload->n);
    if (n != 0U)
    {
        ctx->tm_seq = (uint16_t)((ctx->tm_seq + 1U) & 0x3FFFU);
    }
    return n;
}

uint32_t sb_fsw_poll_tm(SbFsw *ctx, uint8_t *buf, uint32_t cap,
                        int64_t now_gps_ms)
{
    uint8_t pay[200];
    Wr w;
    uint32_t i;
    w.p = pay;
    w.n = 0U;
    w.cap = (uint32_t)sizeof(pay);
    w.ok = true;
    if (cap < SB_FSW_TM_MAX_BYTES)
    {
        return 0U;
    }
    if (ctx->hk_pending)
    {
        uint32_t active = 0U;
        for (i = 0U; i < SB_FSW_MAX_ENCOUNTERS; ++i)
        {
            active += ctx->enc[i].used ? 1U : 0U;
        }
        w_u32(&w, SB_FSW_VERSION);
        w_u32(&w, SB_RULESET_ID);
        w_u8(&w, (uint8_t)ctx->mode);
        w_u32(&w, ctx->selftest_failed);
        w_u32(&w, ctx->cdm_accepted);
        w_u32(&w, ctx->cdm_rejected);
        w_u32(&w, ctx->evaluations);
        w_u32(&w, ctx->seu_corrected);
        w_u32(&w, ctx->seu_uncorrected);
        w_u32(&w, ctx->input_corruptions);
        w_u32(&w, ctx->maneuver_requests);
        w_u8(&w, ctx->last_error);
        w_u8(&w, ctx->last_cdm_error);
        w_u8(&w, (uint8_t)active);
        ctx->hk_pending = false;
        return emit(ctx, buf, cap, ctx->cfg.apid_base, now_gps_ms, &w);
    }
    for (i = 0U; i < SB_FSW_MAX_ENCOUNTERS; ++i)
    {
        SbEncounter *e = &ctx->enc[i];
        const SbDecision *d = &e->decision;
        const uint32_t oi = 1U - (uint32_t)d->self_index;
        if ((!e->used) || ((e->pending_tm & 0x01U) == 0U))
        {
            continue;
        }
        w_u64(&w, d->conjunction_id);
        w_id(&w, &e->cdm.obj[oi].id);
        w_u64(&w, (uint64_t)d->tca_epoch_ms);
        w_f64(&w, d->miss_m);
        w_f64(&w, d->pc);
        w_u8(&w, d->found);
        w_u8(&w, d->actionable);
        w_u8(&w, d->role);
        w_u8(&w, d->rule);
        w_u8(&w, d->advisory);
        w_u8(&w, d->late);
        w_u8(&w, d->plan.required ? 1U : 0U);
        w_u64(&w, (uint64_t)d->burn_epoch_ms);
        w_vec(&w, d->plan.dv_hill);
        w_f64(&w, d->plan.dv_mag);
        w_f64(&w, d->ret.total_dv);
        e->pending_tm = (uint8_t)(e->pending_tm & 0xFEU);
        return emit(ctx, buf, cap, (uint16_t)(ctx->cfg.apid_base + 1U),
                    now_gps_ms, &w);
    }
    for (i = 0U; i < SB_FSW_MAX_ENCOUNTERS; ++i)
    {
        SbEncounter *e = &ctx->enc[i];
        const SbDecision *d = &e->decision;
        if ((!e->used) || ((e->pending_tm & 0x02U) == 0U))
        {
            continue;
        }
        if (ctx->mode != SB_MODE_AUTONOMOUS)
        {
            e->pending_tm = (uint8_t)(e->pending_tm & 0xFDU);
            continue;          /* demoted to ADVISORY since: never command */
        }
        w_u64(&w, d->conjunction_id);
        w_u64(&w, (uint64_t)d->burn_epoch_ms);
        w_vec(&w, d->plan.dv_eci);
        w_vec(&w, d->plan.dv_hill);
        w_f64(&w, d->plan.dv_mag);
        w_u64(&w, (uint64_t)(d->burn_epoch_ms +
                             (int64_t)(d->ret.wait_s * 1000.0)));
        w_vec(&w, d->ret.mirror_dv_hill);
        w_u64(&w, (uint64_t)(d->burn_epoch_ms +
                             (int64_t)((d->ret.wait_s + d->ret.transfer_s) *
                                       1000.0)));
        w_vec(&w, d->ret.final_dv_hill);
        e->pending_tm = (uint8_t)(e->pending_tm & 0xFDU);
        return emit(ctx, buf, cap, (uint16_t)(ctx->cfg.apid_base + 2U),
                    now_gps_ms, &w);
    }
    return 0U;
}

const char *sb_fsw_status_str(SbFswStatus st)
{
    switch (st)
    {
        case SB_FSW_OK:               return "ok";
        case SB_FSW_ERR_MODE:         return "not allowed in this mode";
        case SB_FSW_ERR_CDM:          return "CDM rejected";
        case SB_FSW_ERR_NOT_INVOLVED: return "CDM does not involve this craft";
        case SB_FSW_ERR_TABLE_FULL:   return "table full";
        case SB_FSW_ERR_STALE:        return "TCA already passed";
        case SB_FSW_ERR_FAULT:        return "fault (redundant runs disagree)";
        case SB_FSW_ERR_ARG:          return "invalid argument";
        default:                      return "unknown";
    }
}
