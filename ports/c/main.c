// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// main.c — the SB-C2 --bits transcript. Port of conformance/runner.cpp's
// emit_bits() (and its scaffolding: Fold / circular / crossing_against /
// mk / run_pair / make_sweep_pair). Prints the same 12 lines byte-for-byte.
//
// NOTE (documented deviation): circular() and crossing_against() are test
// scaffolding, not the verdict path. They call libm cos/sin/sqrt exactly as
// the C++ reference does, so the transcript reproduces bit-for-bit. libm trig
// is verified bit-stable across gcc/clang and -O0..-O3 on IEEE-754 targets
// for the (constant) arguments used here.

#include "starboard.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- shared scenario scaffolding --- */
#define R_LEO      (6.93e6)
#define V_LEO      (7600.0)
#define HORIZON_S  (3.0 * 5700.0)
#define EPOCH_MS   (1401000000000ULL)

static uint64_t bits_of(double x)
{
    uint64_t u;
    (void)memcpy(&u, &x, sizeof(u));
    return u;
}

/* FNV-1a fold over 8 little-endian bytes of each 64-bit value. */
typedef struct {
    uint64_t h;
} Fold;

static void fold_init(Fold *f)
{
    f->h = 14695981039346656037ULL;
}

static void fold_add_u64(Fold *f, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i)
    {
        f->h ^= (v >> (8 * i)) & 0xFFULL;
        f->h *= 1099511628211ULL;
    }
}

static void fold_add_d(Fold *f, double d)
{
    fold_add_u64(f, bits_of(d));
}

static void fold_add_v(Fold *f, Vec3 v)
{
    fold_add_d(f, v.x);
    fold_add_d(f, v.y);
    fold_add_d(f, v.z);
}

static Vec3 mkvec(double x, double y, double z)
{
    Vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static CraftDescriptor mk(const char *id, Tier tier, bool can_man,
                          TrustLevel trust, Vec3 pos, Vec3 vel, double var_m2)
{
    CraftDescriptor d;
    d.object_id = sb_make_object_id(id);
    d.tier = tier;
    d.can_maneuver = can_man;
    d.trust = trust;
    d.hard_body_radius_m = SB_DEFAULT_HBR_M;
    d.state.position = pos;
    d.state.velocity = vel;
    d.state = sb_canonicalize(d.state);
    d.covariance = sb_sym33_diagonal(var_m2);
    return d;
}

typedef struct {
    Conjunction    c_ab;
    GiveWayVerdict v_ab;
    GiveWayVerdict v_ba;
    bool           agree;
} PairResult;

static CraftState linear_at(CraftState s, double t)
{
    CraftState r;
    r.position = sb_vadd(s.position, sb_vscale(s.velocity, t));
    r.velocity = s.velocity;
    return r;
}

static PairResult run_pair(const CraftDescriptor *a, const CraftDescriptor *b)
{
    PairResult r;
    const double hbr = a->hard_body_radius_m + b->hard_body_radius_m;
    Conjunction c_ba;
    CraftDescriptor a_tca;
    CraftDescriptor b_tca;

    r.c_ab = sb_assess(a->state, b->state, a->covariance, b->covariance, hbr,
                       HORIZON_S);
    c_ba = sb_assess(b->state, a->state, b->covariance, a->covariance, hbr,
                     HORIZON_S);
    a_tca = *a;
    a_tca.state = linear_at(a->state, r.c_ab.t_ca);
    b_tca = *b;
    b_tca.state = linear_at(b->state, r.c_ab.t_ca);
    r.v_ab = sb_evaluate(&a_tca, &b_tca, EPOCH_MS);
    r.v_ba = sb_evaluate(&b_tca, &a_tca, EPOCH_MS);
    r.agree = sb_id_equal(&r.v_ab.give_way_id, &r.v_ba.give_way_id) &&
              (r.v_ab.deciding_rule == r.v_ba.deciding_rule) &&
              (r.v_ab.conjunction_id == r.v_ba.conjunction_id) &&
              (bits_of(r.c_ab.pc) == bits_of(c_ba.pc));
    return r;
}

/* circular / crossing_against — scaffolding (libm cos/sin/sqrt). */
static CraftState circular(double r, double incl, double theta)
{
    const double v = sqrt(SB_MU_EARTH_M3S2 / r);
    const double ct = cos(theta);
    const double st = sin(theta);
    const double ci = cos(incl);
    const double si = sin(incl);
    CraftState s;
    s.position = mkvec(r * ct, (r * st) * ci, (r * st) * si);
    s.velocity = mkvec(-v * st, (v * ct) * ci, (v * ct) * si);
    return s;
}

static CraftState crossing_against(CraftState ref, double tc, double above,
                                   double tilt)
{
    const CraftState rc = sb_propagate(sb_canonicalize(ref), tc);
    const Vec3 rhat = sb_vnormalized(rc.position);
    const Vec3 vhat = sb_vnormalized(rc.velocity);
    const Vec3 chat = sb_vnormalized(sb_vcross(rhat, vhat));
    const double r_o = sb_vnorm(rc.position) + above;
    const double v_o = sqrt(SB_MU_EARTH_M3S2 / r_o);
    CraftState cross;
    cross.position = sb_vscale(rhat, r_o);
    cross.velocity = sb_vscale(sb_vadd(sb_vscale(vhat, cos(tilt)),
                                       sb_vscale(chat, sin(tilt))), v_o);
    return sb_propagate(cross, -tc);
}

/* --- SB-012 LCG population --- */
static uint64_t lcg_next(uint64_t s)
{
    return (s * 6364136223846793005ULL) + 1442695040888963407ULL;
}

static void make_sweep_pair(int i, uint64_t *s, CraftDescriptor *a,
                            CraftDescriptor *b)
{
    char ida[16];
    char idb[16];
    double pax;
    double pay;
    double paz;
    double vax;
    double vay;
    double vaz;
    double cva;
    double pbx;
    double pby;
    double pbz;
    double vbx;
    double vby;
    double vbz;
    double cvb;

#define RND()  (( *s = lcg_next(*s) ), (double)(*s >> 11) / 9007199254740992.0)
#define RV()   ((2.0 * RND()) - 1.0)

    (void)snprintf(ida, sizeof(ida), "20%02d-%03dA", i % 27, i % 999);
    (void)snprintf(idb, sizeof(idb), "20%02d-%03dB", (i * 7) % 27,
                   (i * 13) % 999);
    pax = RV() * 5.0e4;
    pay = RV() * 5.0e4;
    paz = R_LEO + (RV() * 5.0e4);
    vax = RV() * V_LEO;
    vay = RV() * V_LEO;
    vaz = RV() * 100.0;
    cva = 1.0e3 + (RND() * 1.0e6);
    *a = mk(ida, (Tier)(i % 4), (i % 3) != 0, (TrustLevel)(i % 3),
            mkvec(pax, pay, paz), mkvec(vax, vay, vaz), cva);
    pbx = RV() * 5.0e4;
    pby = RV() * 5.0e4;
    pbz = R_LEO + (RV() * 5.0e4);
    vbx = RV() * V_LEO;
    vby = RV() * V_LEO;
    vbz = RV() * 100.0;
    cvb = 1.0e3 + (RND() * 1.0e6);
    *b = mk(idb, (Tier)((i / 4) % 4), (i % 5) != 0, (TrustLevel)((i / 3) % 3),
            mkvec(pbx, pby, pbz), mkvec(vbx, vby, vbz), cvb);

#undef RND
#undef RV
}

/* --- registry fixtures (the 5 ENTRIES; from registry_fixtures.hpp) --- */
static RegistryEntry make_entry(const char *id, const uint8_t key_id[8],
                                const uint8_t pub[32], uint64_t not_before)
{
    RegistryEntry e;
    int i;
    e.object_id = sb_make_object_id(id);
    for (i = 0; i < 8; ++i)
    {
        e.key_id[i] = key_id[i];
    }
    for (i = 0; i < 32; ++i)
    {
        e.public_key[i] = pub[i];
    }
    e.not_before_ms = not_before;
    return e;
}

static void load_entries(RegistryEntry entries[5])
{
    static const uint8_t k0[8] = {0x0F,0xE8,0x5C,0x53,0x4D,0x81,0xE1,0x9E};
    static const uint8_t p0[32] = {
        0x0F,0xF2,0x59,0x63,0xEE,0x72,0x76,0x0D,0x7F,0xA7,0x9A,0xDE,
        0x49,0xF6,0xB8,0x6F,0x5D,0xDB,0xDB,0xBF,0x63,0xD2,0x27,0xAB,
        0x19,0x5D,0x33,0x9D,0xBA,0x25,0x4D,0x62};
    static const uint8_t k1[8] = {0xDC,0xAE,0x5C,0x46,0x66,0xCD,0xB3,0xD4};
    static const uint8_t p1[32] = {
        0xD5,0xEB,0xAE,0xF9,0x49,0xB9,0x34,0xEB,0xC7,0xBA,0x86,0xA7,
        0x72,0xB7,0xE8,0x19,0x3B,0xA7,0x74,0x56,0x8C,0x51,0xAD,0xE1,
        0x97,0x9B,0x37,0x53,0x85,0x1F,0x3D,0x66};
    static const uint8_t k2[8] = {0x1A,0xDE,0x32,0xF3,0x64,0x26,0x89,0x73};
    static const uint8_t p2[32] = {
        0x65,0x7F,0x2E,0xD2,0xB2,0xF4,0x3E,0x81,0xD2,0x00,0xC8,0x89,
        0xD0,0x51,0xF4,0x21,0x72,0x3B,0x6D,0x88,0xE8,0x93,0x09,0xA3,
        0x88,0xF9,0xC2,0xF8,0x81,0xEE,0xF2,0x97};
    static const uint8_t k3[8] = {0xE6,0xE3,0xC2,0xC5,0x94,0x06,0x96,0x97};
    static const uint8_t p3[32] = {
        0xAF,0x2C,0xD1,0xB6,0xE4,0x70,0xB5,0x3A,0xAB,0x0A,0x5B,0x3A,
        0xF3,0x26,0x9F,0x50,0x9A,0x40,0x9E,0x92,0xC0,0xE3,0xCD,0x6D,
        0xE9,0xA5,0xD9,0x78,0x9C,0x35,0xC5,0xD5};

    entries[0] = make_entry("2026-001A", k0, p0, 1398408000000ULL);
    entries[1] = make_entry("2026-044B", k1, p1, 1398408000000ULL);
    entries[2] = make_entry("1998-067X", k2, p2, 1375080000000ULL);
    entries[3] = make_entry("2025-500C", k3, p3, 1400136000000ULL);
    entries[4] = make_entry("2025-501D", k3, p3, 1400136000000ULL);
}

static void emit_bits(void)
{
    Fold ft;
    Fold fk;
    Fold fp;
    Fold fv;
    Fold fr;
    double R;
    CraftState ka;
    CraftState kb;
    int i;
    OrbitConjunction oc = {0};  /* zero-init: guard the search.* reads below
                                   against a no-conjunction return (the pinned
                                   ka/kb always detect, but do not rely on it) */
    Sym33 cov;
    uint64_t s;
    RegistryEntry entries[5];
    uint8_t nodes[5][SB_HASH_BYTES];
    uint8_t rroot[SB_HASH_BYTES];
    uint32_t ri;
    int im;
    int is;

    printf("starboard-bit-transcript v1 ruleset %08X\n", SB_RULESET_ID);

    printf("exp(-0.5)      %016llX\n",
           (unsigned long long)bits_of(sb_canon_exp(-0.5)));
    printf("sin(1.0)       %016llX\n",
           (unsigned long long)bits_of(sb_canon_sin(1.0)));
    printf("cos(1.0)       %016llX\n",
           (unsigned long long)bits_of(sb_canon_cos(1.0)));

    fold_init(&ft);
    for (i = -4096; i <= 4096; ++i)
    {
        const double x = 0.017 * (double)i;
        double sc;
        double cc;
        sb_canon_sincos(x, &sc, &cc);
        fold_add_d(&ft, sc);
        fold_add_d(&ft, cc);
        if (x <= 0.0)
        {
            fold_add_d(&ft, sb_canon_exp(x));
        }
    }
    printf("transcendental %016llX\n", (unsigned long long)ft.h);

    R = 6.93e6;
    ka = sb_canonicalize(circular(R, 0.0, 0.0));
    kb = sb_canonicalize(crossing_against(ka, 800.0, 150.0, 0.25 * SB_PI));
    fold_init(&fk);
    for (i = 0; i <= 256; ++i)
    {
        const double t = 4.0 * (double)i;
        const CraftState sa = sb_propagate(ka, t);
        const CraftState sb = sb_propagate(kb, t);
        fold_add_v(&fk, sa.position);
        fold_add_v(&fk, sa.velocity);
        fold_add_v(&fk, sb.position);
        fold_add_v(&fk, sb.velocity);
    }
    printf("ephemeris      %016llX\n", (unsigned long long)fk.h);

    cov = sb_sym33_diagonal(9.0e4);
    (void)sb_screen_orbits(ka, kb, cov, cov, 20.0, SB_SCREEN_ARC_MAX_S, &oc);
    printf("search.tca     %016llX\n",
           (unsigned long long)bits_of(oc.conj.t_ca));
    printf("search.miss    %016llX\n",
           (unsigned long long)bits_of(oc.conj.miss_distance));
    printf("search.pc      %016llX\n",
           (unsigned long long)bits_of(oc.conj.pc));

    fold_init(&fp);
    for (im = 1; im <= 20; ++im)
    {
        for (is = 1; is <= 20; ++is)
        {
            Conjunction c;
            Sym33 cc;
            c.detected = true;
            c.t_ca = 100.0;
            c.miss_vector = mkvec(0.0, 0.0, 128.0 * (double)im);
            c.miss_distance = sb_vnorm(c.miss_vector);
            c.rel_velocity = mkvec(7000.0, -3000.0, 0.0);
            c.pc = 0.0;
            cc = sb_sym33_diagonal(1024.0 * (double)(is * is));
            fold_add_d(&fp,
                       sb_starboard_pc(&c, cc, 5.0 + (2.0 * (double)im)));
        }
    }
    printf("pc-kernel      %016llX\n", (unsigned long long)fp.h);

    fold_init(&fv);
    s = 0x5EEDULL;
    for (i = 0; i < 4096; ++i)
    {
        CraftDescriptor a;
        CraftDescriptor b;
        PairResult r;
        int j;
        make_sweep_pair(i, &s, &a, &b);
        r = run_pair(&a, &b);
        fold_add_u64(&fv, r.v_ab.conjunction_id);
        fold_add_u64(&fv, (uint64_t)r.v_ab.deciding_rule);
        fold_add_d(&fv, r.c_ab.pc);
        for (j = 0; j < 12; ++j)
        {
            fold_add_u64(&fv,
                (uint64_t)(unsigned char)r.v_ab.give_way_id.c[j]);
        }
    }
    printf("verdict-sweep  %016llX\n", (unsigned long long)fv.h);

    fold_init(&fr);
    load_entries(entries);
    for (ri = 0U; ri < 5U; ++ri)
    {
        sb_leaf_hash(&entries[ri], nodes[ri]);
    }
    sb_merkle_root(nodes, 5U, rroot);
    for (ri = 0U; ri < SB_HASH_BYTES; ri += 8U)
    {
        uint64_t w = 0U;
        uint32_t j;
        for (j = 0U; j < 8U; ++j)
        {
            w |= (uint64_t)rroot[ri + j] << (8U * j);
        }
        fold_add_u64(&fr, w);
    }
    printf("registry       %016llX\n", (unsigned long long)fr.h);
}

int main(void)
{
    if (!sb_arithmetic_profile_ok())
    {
        fprintf(stderr, "FATAL: FP contraction detected (SPEC §3.5) — this "
                        "build cannot produce conformant verdicts.\n");
        return 2;
    }
    emit_bits();
    return 0;
}
