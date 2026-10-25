// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// starboard.h — types, constants and prototypes for the C99 verdict-path port.
// This is a MISRA-C99-oriented transliteration of the C++ reference headers
// (canon/id/geometry/propagator/covariance/screening/rules/trust/multibody and
// crypto/sha512 + registry). It reproduces the --bits transcript emitted by
// conformance/runner.cpp byte-for-byte.

#ifndef STARBOARD_H
#define STARBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Disable FP contraction in-source (SPEC §3.5): a fused multiply-add would
// change verdict-path bits, breaking cross-vendor agreement (demonstrated: a
// -mfma contraction-on build shifts pc-kernel and verdict-sweep). Every TU
// that computes verdict-path values includes this header.
//   * Clang / conforming C11 compilers honor this pragma — verified to hold
//     contraction OFF even under -mfma, so the guarantee is in-source there.
//   * GCC's C frontend does NOT implement the STDC FP_CONTRACT pragma (it
//     warns "unknown pragma"), so it is suppressed for GCC; a conformant GCC
//     build MUST pass -ffp-contract=off instead (the Makefile does).
// Belt (pragma) and suspenders (flag) — the SPEC requires the outcome; both
// mechanisms enforce it.
#if defined(__clang__) || !defined(__GNUC__)
#pragma STDC FP_CONTRACT OFF
#endif

// The whole transcript is defined over the IEEE-754 binary64 encoding; a
// non-8-byte double would silently mis-encode it. Fail to compile if so.
typedef char sb_assert_double_is_8_bytes[(sizeof(double) == 8) ? 1 : -1];

// ---------------------------------------------------------------------------
// §3.4 Quantization lattice (exact powers of two).
// ---------------------------------------------------------------------------
#define SB_Q_POS_M    (0.0009765625)         /* 2^-10 m   */
#define SB_Q_VEL_MPS  (9.5367431640625e-07)  /* 2^-20 m/s */
#define SB_Q_COV_M2   (9.5367431640625e-07)  /* 2^-20 m^2 */
#define SB_Q_TIME_S   (0.0009765625)         /* 2^-10 s   */

// ---------------------------------------------------------------------------
// §3.6 Canonical physical constants.
// ---------------------------------------------------------------------------
#define SB_MU_EARTH_M3S2  (3.986004418e14)
#define SB_J2_EARTH       (1.08262668e-3)
#define SB_R_EARTH_M      (6378137.0)
#define SB_PI             (3.141592653589793)

// ---------------------------------------------------------------------------
// §4 Screening & actionability.
// ---------------------------------------------------------------------------
#define SB_R_SCREEN_M     (8192.0)
#define SB_R_SAFE_M       (1024.0)
#define SB_PC_ACTIONABLE  (1.0e-4)
#define SB_DEFAULT_HBR_M  (10.0)
#define SB_PATERA_NODES   (32)
#define SB_VAR_FLOOR_M2   (1.0)
#define SB_DEFAULT_VAR_M2 (1.0e6)

// ---------------------------------------------------------------------------
// §4.2 Ephemeris & conjunction search.
// ---------------------------------------------------------------------------
#define SB_PROP_STEP_S       (4.0)
#define SB_SCREEN_ARC_MAX_S  (1024.0)
#define SB_SCREEN_STEP_S     (32.0)
#define SB_TCA_BISECT_ITERS  (48)
#define SB_PROP_DT_MAX_S     (1048576.0)     /* 2^20 s, SPEC §3.7 */

// ---------------------------------------------------------------------------
// §5 Rule predicates.
// ---------------------------------------------------------------------------
#define SB_OVERTAKE_COS   (0.3826834323650898)
#define SB_EPS_SPEED_MPS  (0.0625)
#define SB_EPS_LATERAL_M  (1.0)
#define SB_EPS_BEARING    (0.0625)

// ---------------------------------------------------------------------------
// §6 Maneuver, §7 Multi-body.
// ---------------------------------------------------------------------------
#define SB_MANEUVER_MARGIN        (1.25)
#define SB_MIN_LEAD_S             (1.0)
#define SB_RETURN_PHASING_ORBITS  (2)
#define SB_MAX_WINDOW_CRAFT       (32U)
#define SB_MAX_WINDOW_CONJ        (64U)

// ---------------------------------------------------------------------------
// §11 identity of the ruleset itself.
// ---------------------------------------------------------------------------
#define SB_RULESET_ID     (0x53420001U)
#define SB_TCA_BUCKET_MS  (16384U)

// ===========================================================================
// canon.hpp — quantization + pinned transcendentals.
// ===========================================================================
double sb_quantize(double x, double q);
double sb_canon_exp(double x);
void   sb_canon_sincos(double x, double *sin_out, double *cos_out);
double sb_canon_sin(double x);
double sb_canon_cos(double x);
bool   sb_arithmetic_profile_ok(void);

// ===========================================================================
// id.hpp — canonical object identity.
// ===========================================================================
typedef struct {
    char c[12];
} ObjectId;

ObjectId sb_make_object_id(const char *s);
int      sb_id_compare(const ObjectId *a, const ObjectId *b);
bool     sb_id_equal(const ObjectId *a, const ObjectId *b);

// ===========================================================================
// geometry.hpp — Vec3 and encounter kernel geometry.
// ===========================================================================
typedef struct {
    double x, y, z;
} Vec3;

Vec3   sb_vadd(Vec3 a, Vec3 b);
Vec3   sb_vsub(Vec3 a, Vec3 b);
Vec3   sb_vscale(Vec3 a, double s);
Vec3   sb_vdiv(Vec3 a, double s);
double sb_vdot(Vec3 a, Vec3 b);
Vec3   sb_vcross(Vec3 a, Vec3 b);
double sb_vnorm2(Vec3 a);
double sb_vnorm(Vec3 a);
Vec3   sb_vnormalized(Vec3 a);
Vec3   sb_vquantize(Vec3 v, double q);

typedef struct {
    Vec3 position;
    Vec3 velocity;
} CraftState;

CraftState sb_canonicalize(CraftState s);

typedef struct {
    bool   detected;
    double t_ca;
    double miss_distance;
    Vec3   miss_vector;
    Vec3   rel_velocity;
    double pc;
} Conjunction;

Conjunction sb_screen(CraftState a, CraftState b, double horizon_s,
                      double r_screen);

// ===========================================================================
// propagator.hpp — canonical J2 propagator.
// ===========================================================================
Vec3       sb_accel_j2(Vec3 r);
void       sb_rk4_step(CraftState *s, double h);
CraftState sb_propagate(CraftState s0, double dt);

// ===========================================================================
// covariance.hpp — Sym33 + Starboard-Pc.
// ===========================================================================
typedef struct {
    double xx, xy, xz, yy, yz, zz;
} Sym33;

Sym33  sb_sym33_diagonal(double v);
Sym33  sb_cov_add(Sym33 a, Sym33 b);
Sym33  sb_cov_quantize(Sym33 c);
Sym33  sb_combined_covariance(Sym33 cov_a, Sym33 cov_b);
double sb_cov_quad(Sym33 c, Vec3 u, Vec3 v);
Vec3   sb_pinned_perpendicular(Vec3 n);
double sb_starboard_pc(const Conjunction *conj, Sym33 c_comb, double r_hb);
bool   sb_actionable(const Conjunction *conj);
Conjunction sb_assess(CraftState a, CraftState b, Sym33 cov_a, Sym33 cov_b,
                      double r_hb, double horizon_s);

// ===========================================================================
// screening.hpp — canonical conjunction search.
// ===========================================================================
typedef struct {
    Conjunction conj;
    CraftState  a_tca;
    CraftState  b_tca;
} OrbitConjunction;

void sb_radial_bounds(CraftState s, double *r_lo, double *r_hi);
bool sb_screen_orbits(CraftState a0_in, CraftState b0_in,
                      Sym33 cov_a, Sym33 cov_b, double r_hb, double arc_s,
                      OrbitConjunction *out);

// ===========================================================================
// trust.hpp / rules.hpp — Layer 1.
// ===========================================================================
typedef enum {
    TIER_DEORBITING  = 0,
    TIER_OPERATIONAL = 1,
    TIER_CRITICAL    = 2,
    TIER_CREWED      = 3
} Tier;

typedef enum {
    TRUST_UNKNOWN  = 0,
    TRUST_ASSERTED = 1,
    TRUST_VERIFIED = 2
} TrustLevel;

typedef enum {
    ROLE_GIVE_WAY = 0,
    ROLE_STAND_ON = 1
} Role;

typedef enum {
    RULE_CAPABILITY = 1,
    RULE_TIER       = 2,
    RULE_OVERTAKING = 3,
    RULE_STARBOARD  = 4,
    RULE_FAIR_COIN  = 5,
    RULE_CASCADE    = 6
} DecidingRule;

bool sb_effective_can_maneuver(TrustLevel trust, bool asserted);
Tier sb_effective_tier(TrustLevel trust, Tier asserted);

typedef struct {
    ObjectId   object_id;
    Tier       tier;
    bool       can_maneuver;
    TrustLevel trust;
    double     hard_body_radius_m;
    CraftState state;
    Sym33      covariance;
} CraftDescriptor;

typedef struct {
    Role         self_role;
    DecidingRule deciding_rule;
    ObjectId     give_way_id;
    uint64_t     conjunction_id;
    bool         advisory;
} GiveWayVerdict;

#define SB_CONJ_ID_MSG_BYTES (36U)
void     sb_conjunction_id_message(const ObjectId *a, const ObjectId *b,
                                   uint64_t tca_epoch_ms,
                                   uint8_t msg[SB_CONJ_ID_MSG_BYTES]);
uint64_t sb_fnv1a64(const uint8_t *msg, uint32_t n);
uint64_t sb_conjunction_id(const ObjectId *a, const ObjectId *b,
                           uint64_t tca_epoch_ms);
GiveWayVerdict sb_evaluate(const CraftDescriptor *self,
                           const CraftDescriptor *other,
                           uint64_t tca_epoch_ms);

// ===========================================================================
// frames.hpp / maneuver.hpp — Hill frame, CW dynamics, give-way burn (§6).
// ===========================================================================
typedef struct {
    double m[3][3];
} Mat3;

typedef struct {
    Vec3 r;
    Vec3 v;
} HillState;

typedef struct {
    bool   required;
    Vec3   dv_eci;
    Vec3   dv_hill;
    double dv_mag;
    double displacement_m;
} GiveWayPlan;

typedef struct {
    Vec3   mirror_dv_hill;
    Vec3   final_dv_hill;
    double wait_s;
    double transfer_s;
    double total_dv;
} MirrorReturnPlan;

typedef struct {
    Vec3   dv1_hill;
    Vec3   dv2_hill;
    double transfer_s;
    double total_dv;
} ReturnPlan;

Mat3      sb_mat3_identity(void);
Vec3      sb_mat3_mul(const Mat3 *a, Vec3 v);
Vec3      sb_mat3_tmul(const Mat3 *a, Vec3 v);
Vec3      sb_mat3_solve(const Mat3 *a, Vec3 b);
Mat3      sb_hill_basis(CraftState s);
double    sb_mean_motion(CraftState s);
Mat3      sb_cw_phi_rr(double n, double t);
Mat3      sb_cw_phi_rv(double n, double t);
Mat3      sb_cw_phi_vr(double n, double t);
Mat3      sb_cw_phi_vv(double n, double t);
HillState sb_cw_propagate(HillState x0, double n, double t);
GiveWayPlan sb_plan_give_way(CraftState self_now, CraftState self_tca,
                             const Conjunction *conj,
                             bool self_is_canonical_lo, double r_safe);
MirrorReturnPlan sb_plan_mirror_return(Vec3 giveway_dv_hill, double n,
                                       int phasing_orbits);
ReturnPlan sb_plan_return(HillState offset, double n);

// ===========================================================================
// multibody.hpp — Layer 7 window cascade (§7).
// ===========================================================================
typedef struct {
    uint32_t   a;
    uint32_t   b;
    uint64_t   tca_epoch_ms;
    CraftState a_tca;
    CraftState b_tca;
} WindowEntry;

typedef struct {
    uint64_t     conjunction_id;
    uint32_t     give_way_index;
    uint32_t     stand_on_index;
    DecidingRule deciding_rule;
    bool         escalated;
    bool         advisory;
} WindowVerdict;

uint32_t sb_resolve_window(const CraftDescriptor *craft, uint32_t n_craft,
                           WindowEntry *entries, uint32_t n_entries,
                           WindowVerdict *out);

// ===========================================================================
// broadphase.hpp — Stage-1 radial-band sweep (§4.8).
// ===========================================================================
typedef struct {
    uint32_t i;
    uint32_t j;
} SbPair;

typedef struct {
    double   lo;
    double   hi;
    uint32_t idx;
} SbBand;

uint32_t sb_radial_band_pairs(const CraftState *objs, uint32_t n,
                              SbBand *bands, uint32_t *active,
                              SbPair *out, uint32_t cap, bool *overflow,
                              double margin);

// ===========================================================================
// crypto/sha512.hpp
// ===========================================================================
typedef struct {
    uint64_t h[8];
    uint64_t total_lo;
    uint64_t total_hi;
    uint8_t  buf[128];
    uint64_t buf_len;
} Sha512State;

void sb_sha512_init(Sha512State *st);
void sb_sha512_update(Sha512State *st, const uint8_t *data, uint64_t len);
void sb_sha512_final(Sha512State *st, uint8_t out[64]);
void sb_sha512(const uint8_t *msg, uint64_t len, uint8_t out[64]);

// ===========================================================================
// registry.hpp — Merkle over SHA-512.
// ===========================================================================
#define SB_HASH_BYTES  (64U)
#define SB_ENTRY_BYTES (75U)

typedef struct {
    ObjectId object_id;
    uint8_t  key_id[8];
    uint8_t  public_key[32];
    uint64_t not_before_ms;
} RegistryEntry;

void sb_leaf_hash(const RegistryEntry *e, uint8_t out[SB_HASH_BYTES]);
void sb_node_hash(const uint8_t left[SB_HASH_BYTES],
                  const uint8_t right[SB_HASH_BYTES],
                  uint8_t out[SB_HASH_BYTES]);
void sb_merkle_root(uint8_t (*nodes)[SB_HASH_BYTES], uint32_t n,
                    uint8_t out[SB_HASH_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* STARBOARD_H */
