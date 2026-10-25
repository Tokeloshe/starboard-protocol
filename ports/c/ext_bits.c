// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// ext_bits.c — the EXTENDED cross-language transcript (suite v2, informative).
// Emits the same lines as conformance/ext_bits.cpp (the C++ reference), so the
// C99 port is cross-checked bit-for-bit on the surfaces the normative v1
// transcript does not cover: CW maneuver planning, both returns, the multi-
// body cascade and the broad-phase candidate set. Scenario construction uses
// ONLY + - * / sqrt (no libm trig), so the transcript is also a clean
// cross-ARCHITECTURE probe: any divergence is the library under test, never
// the host's libm. Golden: conformance/expected_bits_ext.txt.

#include "starboard.h"
#include "ext_scenarios.h"
#include "starboard_cdm.h"
#include "../../conformance/ext_cdm_gen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t h;
} Fold;

static uint64_t bits_of(double x)
{
    uint64_t u;
    (void)memcpy(&u, &x, sizeof(u));
    return u;
}

static void fold_init(Fold *f)
{
    f->h = 14695981039346656037ULL;
}

static void fold_u64(Fold *f, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i)
    {
        f->h ^= (v >> (8 * i)) & 0xFFULL;
        f->h *= 1099511628211ULL;
    }
}

static void fold_d(Fold *f, double d)
{
    fold_u64(f, bits_of(d));
}

static void fold_v(Fold *f, Vec3 v)
{
    fold_d(f, v.x);
    fold_d(f, v.y);
    fold_d(f, v.z);
}

static void emit(const char *name, const Fold *f)
{
    printf("%-14s %016llX\n", name, (unsigned long long)f->h);
}

static void ext_maneuver(void)
{
    Fold fm;
    Fold fr;
    uint64_t s = 0xC0FFEEULL;
    int i;
    fold_init(&fm);
    fold_init(&fr);
    for (i = 0; i < 2048; ++i)
    {
        CraftState a;
        CraftState b;
        Conjunction c;
        GiveWayPlan pa;
        GiveWayPlan pb;
        MirrorReturnPlan mr;
        ReturnPlan rp;
        HillState off;
        double n;
        double t_ca;
        CraftState a_tca;
        CraftState b_tca;
        ext_encounter(&s, &a, &b, &t_ca);
        c = sb_assess(a, b, sb_sym33_diagonal(4.0e4), sb_sym33_diagonal(9.0e4),
                      20.0, 6000.0);
        a_tca = ext_linear_at(sb_canonicalize(a), c.t_ca);
        b_tca = ext_linear_at(sb_canonicalize(b), c.t_ca);
        pa = sb_plan_give_way(sb_canonicalize(a), a_tca, &c, true, SB_R_SAFE_M);
        {
            Conjunction cb = c;
            cb.miss_vector = sb_vscale(c.miss_vector, -1.0);
            cb.rel_velocity = sb_vscale(c.rel_velocity, -1.0);
            pb = sb_plan_give_way(sb_canonicalize(b), b_tca, &cb, false,
                                  SB_R_SAFE_M);
        }
        fold_u64(&fm, pa.required ? 1U : 0U);
        fold_v(&fm, pa.dv_eci);
        fold_v(&fm, pa.dv_hill);
        fold_d(&fm, pa.dv_mag);
        fold_d(&fm, pa.displacement_m);
        fold_v(&fm, pb.dv_eci);
        fold_d(&fm, pb.displacement_m);

        n = sb_mean_motion(sb_canonicalize(a));
        mr = sb_plan_mirror_return(pa.dv_hill, n, SB_RETURN_PHASING_ORBITS);
        fold_v(&fr, mr.mirror_dv_hill);
        fold_v(&fr, mr.final_dv_hill);
        fold_d(&fr, mr.wait_s);
        fold_d(&fr, mr.transfer_s);
        fold_d(&fr, mr.total_dv);
        off.r.x = ext_sym(&s) * 800.0;
        off.r.y = ext_sym(&s) * 3000.0;
        off.r.z = ext_sym(&s) * 400.0;
        off.v.x = ext_sym(&s) * 0.5;
        off.v.y = ext_sym(&s) * 0.5;
        off.v.z = ext_sym(&s) * 0.5;
        rp = sb_plan_return(off, n);
        fold_v(&fr, rp.dv1_hill);
        fold_v(&fr, rp.dv2_hill);
        fold_d(&fr, rp.transfer_s);
        fold_d(&fr, rp.total_dv);
        {
            const HillState x = sb_cw_propagate(off, n, 0.37 * rp.transfer_s);
            fold_v(&fr, x.r);
            fold_v(&fr, x.v);
        }
    }
    emit("maneuver", &fm);
    emit("returns", &fr);
}

static void ext_window(void)
{
    Fold fw;
    uint64_t s = 0x3B0D7ULL;
    int w;
    fold_init(&fw);
    for (w = 0; w < 512; ++w)
    {
        CraftDescriptor craft[8];
        WindowEntry win[12];
        WindowVerdict out[12];
        uint32_t n_craft = 3U + (uint32_t)(ext_uniform(&s) * 6.0);
        uint32_t n_ent = 2U + (uint32_t)(ext_uniform(&s) * 11.0);
        uint32_t k;
        uint32_t got;
        for (k = 0U; k < n_craft; ++k)
        {
            ext_descriptor(&s, w, (int)k, &craft[k]);
        }
        for (k = 0U; k < n_ent; ++k)
        {
            uint32_t ia = (uint32_t)(ext_uniform(&s) * (double)n_craft);
            uint32_t ib = (uint32_t)(ext_uniform(&s) * (double)n_craft);
            if (ia == ib)
            {
                ib = (ia + 1U) % n_craft;
            }
            win[k].a = ia;
            win[k].b = ib;
            win[k].tca_epoch_ms = EXT_EPOCH_MS +
                (uint64_t)(ext_uniform(&s) * 90000.0);
            win[k].a_tca = craft[ia].state;
            win[k].b_tca = craft[ib].state;
        }
        got = sb_resolve_window(craft, n_craft, win, n_ent, out);
        fold_u64(&fw, (uint64_t)got);
        for (k = 0U; k < got; ++k)
        {
            fold_u64(&fw, out[k].conjunction_id);
            fold_u64(&fw, (uint64_t)out[k].give_way_index);
            fold_u64(&fw, (uint64_t)out[k].stand_on_index);
            fold_u64(&fw, (uint64_t)out[k].deciding_rule);
            fold_u64(&fw, out[k].escalated ? 1U : 0U);
            fold_u64(&fw, out[k].advisory ? 1U : 0U);
        }
    }
    emit("window", &fw);
}

/* Broad-phase: the candidate SET (canonically ordered by (i, j)) — the order
 * a sweep emits pairs in is not normative, the set is. Counting-matrix
 * canonicalization: flag each pair, then fold in (i, j) order. */
#define EXT_BP_N (240U)
static uint8_t g_bp_flag[EXT_BP_N][EXT_BP_N];
static SbPair g_bp_out[(EXT_BP_N * EXT_BP_N) / 2U];

static void ext_broadphase(void)
{
    Fold fb;
    CraftState objs[EXT_BP_N];
    SbBand bands[EXT_BP_N];
    uint32_t active[EXT_BP_N];
    uint64_t s = 0xB0A2DULL;
    uint32_t i;
    uint32_t j;
    uint32_t nc;
    bool ovf = false;
    fold_init(&fb);
    for (i = 0U; i < EXT_BP_N; ++i)
    {
        objs[i] = ext_orbit_state(&s);
    }
    nc = sb_radial_band_pairs(objs, EXT_BP_N, bands, active, g_bp_out,
                              (uint32_t)(sizeof(g_bp_out) / sizeof(g_bp_out[0])),
                              &ovf, SB_R_SCREEN_M);
    (void)memset(g_bp_flag, 0, sizeof(g_bp_flag));
    for (i = 0U; i < nc; ++i)
    {
        g_bp_flag[g_bp_out[i].i][g_bp_out[i].j] = 1U;
    }
    fold_u64(&fb, (uint64_t)nc);
    fold_u64(&fb, ovf ? 1U : 0U);
    for (i = 0U; i < EXT_BP_N; ++i)
    {
        for (j = i + 1U; j < EXT_BP_N; ++j)
        {
            if (g_bp_flag[i][j] != 0U)
            {
                fold_u64(&fb, ((uint64_t)i << 32) | (uint64_t)j);
            }
        }
    }
    emit("broadphase", &fb);
}

static const char *const EXT_DECIMALS[] = {
    "0.1", "1E22", "1e-22", "1E23", "1e-23", "999999999999999",
    "9999999999999999", "-0", "+.5", ".5e1", "5.", "1e+0003", "1e00001",
    "2570.097065", "-1.4372E+03", "000000000000000000123.45", "1.2.3", "e5",
    "4.835E-05", "6.000000000000000000", "123456789012345.0000", "", "-",
    "7e", "0.000000000000000000001", "3.14159265358979"
};

static void ext_cdm(void)
{
    Fold fd;
    Fold fc;
    uint64_t s = 0xCD3B1ULL;
    uint32_t i;
    int k;
    static char text[8192];
    fold_init(&fd);
    fold_init(&fc);
    for (i = 0U; i < (uint32_t)(sizeof(EXT_DECIMALS) / sizeof(EXT_DECIMALS[0]));
         ++i)
    {
        int sc;
        for (sc = -3; sc <= 3; sc += 3)
        {
            double v = 0.0;
            const SbCdmStatus st = sb_decimal_to_double(
                EXT_DECIMALS[i], (uint32_t)strlen(EXT_DECIMALS[i]), sc, &v);
            fold_u64(&fd, (uint64_t)st);
            fold_d(&fd, v);
        }
    }
    emit("decimal", &fd);
    for (k = 0; k < 400; ++k)
    {
        SbCdm m;
        const uint32_t n = gen_cdm(&s, text, (uint32_t)sizeof(text));
        const SbCdmStatus st = sb_cdm_parse(text, n, &m, NULL);
        fold_u64(&fc, (uint64_t)st);
        if (st == SB_CDM_OK)
        {
            OrbitConjunction oc;
            int64_t tca = 0;
            uint32_t o;
            for (o = 0U; o < 2U; ++o)
            {
                uint32_t j;
                for (j = 0U; j < 12U; ++j)
                {
                    fold_u64(&fc, (uint64_t)(uint8_t)m.obj[o].id.c[j]);
                }
                fold_u64(&fc, (uint64_t)m.obj[o].frame);
                fold_u64(&fc, (uint64_t)m.obj[o].maneuverable);
                fold_u64(&fc, (uint64_t)m.obj[o].object_type);
                fold_v(&fc, m.obj[o].state_tca.position);
                fold_v(&fc, m.obj[o].state_tca.velocity);
                fold_d(&fc, m.obj[o].covariance.xx);
                fold_d(&fc, m.obj[o].covariance.xy);
                fold_d(&fc, m.obj[o].covariance.xz);
                fold_d(&fc, m.obj[o].covariance.yy);
                fold_d(&fc, m.obj[o].covariance.yz);
                fold_d(&fc, m.obj[o].covariance.zz);
            }
            fold_u64(&fc, (uint64_t)m.tca_gps_ms);
            fold_d(&fc, m.cdm_miss_m);
            fold_d(&fc, m.hbr_m);
            fold_u64(&fc, sb_cdm_screen(&m, &oc, &tca) ? 1U : 0U);
            fold_d(&fc, oc.conj.t_ca);
            fold_d(&fc, oc.conj.miss_distance);
            fold_d(&fc, oc.conj.pc);
            fold_u64(&fc, (uint64_t)tca);
        }
    }
    emit("cdm-binding", &fc);
}

int main(void)
{
    if (!sb_arithmetic_profile_ok())
    {
        fprintf(stderr, "FATAL: FP contraction detected (SPEC §3.5)\n");
        return 2;
    }
    printf("starboard-ext-transcript v2 ruleset %08X\n", SB_RULESET_ID);
    ext_maneuver();
    ext_window();
    ext_broadphase();
    ext_cdm();
    return 0;
}
