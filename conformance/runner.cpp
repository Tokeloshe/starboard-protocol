// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.

// ============================================================================
// Starboard CONFORMANCE SUITE  —  ruleset SB/1  —  suite version 1
//
// These vectors ARE the definition of "Starboard compatible"
// (docs/CONFORMANCE.md). A candidate implementation earns:
//   SB-C1 (verdict conformance)  by reproducing every expected verdict, and
//   SB-C2 (bit conformance)      by matching every pinned bit pattern.
//
// Every scenario is additionally evaluated from BOTH craft's perspectives —
// each vector is simultaneously a test of Theorem 1 (agreement).
// ============================================================================

#include "starboard/starboard.hpp"

#include "registry_fixtures.hpp"
#include "support.hpp"

#include <cstdio>
#include <cstring>

using namespace starboard;

namespace {

int g_failures = 0;

using sbtest::bits_of;
using sbtest::circular;
using sbtest::crossing_against;
using sbtest::lcg_next;

void report(const char* id, const char* name, bool pass) {
    std::printf("  %-8s %-38s %s\n", id, name, pass ? "PASS" : "FAIL");
    if (!pass) { ++g_failures; }
}

// --- shared scenario scaffolding -------------------------------------------
constexpr double R_LEO = 6.93e6;
constexpr double V_LEO = 7600.0;
constexpr double HORIZON_S = 3.0 * 5700.0;
constexpr uint64_t EPOCH_MS = 1401000000000ull;

// SB-C2 bit anchors (pinned from the reference run).
constexpr uint64_t SB001_PC_BITS   = 0x3F5117B3E9258534ull; // linear kernel Pc
constexpr uint64_t SB013_TCA_BITS  = 0x4089000000000000ull; // 800.0 s exact
constexpr uint64_t SB013_PC_BITS   = 0x3F5117B4A88B614Cull; // 1.043e-03
constexpr uint64_t SB014_SIN1_BITS = 0x3FEAED548F090CEEull;
constexpr uint64_t SB014_COS1_BITS = 0x3FE14A280FB5068Cull;
constexpr uint64_t SB014_SINM_BITS = 0x3F4237685A41A8BDull;

CraftDescriptor mk(const char* id, Tier tier, bool can_man, TrustLevel trust,
                   const Vec3& pos, const Vec3& vel, double var_m2) {
    CraftDescriptor d;
    d.object_id = make_object_id(id);
    d.tier = tier;
    d.can_maneuver = can_man;
    d.trust = trust;
    d.state.position = pos;
    d.state.velocity = vel;
    d.state = canonicalize(d.state);
    d.covariance = Sym33::diagonal(var_m2);
    return d;
}

struct PairResult {
    Conjunction c_ab;
    GiveWayVerdict v_ab, v_ba;
    bool agree;
};

// Advance a canonical state linearly by t (the kernel vectors are specified
// in the linear relative-motion model, where this is the exact TCA state).
CraftState linear_at(const CraftState& s, double t) {
    CraftState r;
    r.position = s.position + s.velocity * t;
    r.velocity = s.velocity;
    return r;
}

PairResult run_pair(const CraftDescriptor& a, const CraftDescriptor& b) {
    PairResult r;
    const double hbr = a.hard_body_radius_m + b.hard_body_radius_m;
    r.c_ab = assess(a.state, b.state, a.covariance, b.covariance, hbr,
                    HORIZON_S);
    Conjunction c_ba = assess(b.state, a.state, b.covariance, a.covariance,
                              hbr, HORIZON_S);
    // Rule geometry is evaluated at the encounter (SPEC §5.1).
    CraftDescriptor a_tca = a;
    a_tca.state = linear_at(a.state, r.c_ab.t_ca);
    CraftDescriptor b_tca = b;
    b_tca.state = linear_at(b.state, r.c_ab.t_ca);
    r.v_ab = evaluate(a_tca, b_tca, EPOCH_MS);
    r.v_ba = evaluate(b_tca, a_tca, EPOCH_MS);
    r.agree = id_equal(r.v_ab.give_way_id, r.v_ba.give_way_id) &&
              (r.v_ab.deciding_rule == r.v_ba.deciding_rule) &&
              (r.v_ab.conjunction_id == r.v_ba.conjunction_id) &&
              (bits_of(r.c_ab.pc) == bits_of(c_ba.pc));
    return r;
}

bool expect(const PairResult& r, DecidingRule rule, const char* give_way) {
    return r.agree && (r.v_ab.deciding_rule == rule) &&
           id_equal(r.v_ab.give_way_id, make_object_id(give_way));
}

// The canonical crossing pair (also the demo's Act I geometry).
CraftDescriptor crossing_a() {
    return mk("2026-001A", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
              Vec3(0.0, 0.0, R_LEO), Vec3(V_LEO, 0.0, 0.0), 9.0e4);
}
CraftDescriptor crossing_b() {
    return mk("2026-044B", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
              Vec3(V_LEO * 2700.0, -V_LEO * 2700.0, R_LEO + 150.0),
              Vec3(0.0, V_LEO, 0.0), 9.0e4);
}

// ---------------------------------------------------------------------------
// SB-001  crossing -> Rule 4 (starboard), and the SB-C2 Pc bit anchor.
// ---------------------------------------------------------------------------
void sb001() {
    const PairResult r = run_pair(crossing_a(), crossing_b());
    const bool pc_bits_ok = (bits_of(r.c_ab.pc) == SB001_PC_BITS);
    report("SB-001", "crossing: starboard craft gives way",
           expect(r, DecidingRule::STARBOARD, "2026-001A") &&
           actionable(r.c_ab) && pc_bits_ok);
}

// ---------------------------------------------------------------------------
// SB-002  overtaking -> Rule 3, the faster chaser gives way.
// ---------------------------------------------------------------------------
void sb002() {
    const CraftDescriptor tgt =
        mk("2026-100A", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
           Vec3(0.0, 0.0, R_LEO), Vec3(V_LEO, 0.0, 0.0), 9.0e4);
    const CraftDescriptor chaser =
        mk("2026-200B", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
           Vec3(-3000.0, 0.0, R_LEO + 30.0), Vec3(V_LEO + 50.0, 0.0, 0.0),
           9.0e4);
    const PairResult r = run_pair(tgt, chaser);
    report("SB-002", "overtaking: chaser gives way",
           expect(r, DecidingRule::OVERTAKING, "2026-200B") &&
           actionable(r.c_ab));
}

// ---------------------------------------------------------------------------
// SB-003  head-on, lateral offset: symmetric starboard -> fair coin.
// ---------------------------------------------------------------------------
void sb003() {
    const CraftDescriptor a =
        mk("2026-300A", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
           Vec3(0.0, 0.0, R_LEO), Vec3(V_LEO, 0.0, 0.0), 9.0e4);
    const CraftDescriptor b =
        mk("2026-300B", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
           Vec3(2.0 * V_LEO * 100.0, -80.0, R_LEO), Vec3(-V_LEO, 0.0, 0.0),
           9.0e4);
    const PairResult r = run_pair(a, b);
    report("SB-003", "head-on: fair coin decides",
           expect(r, DecidingRule::FAIR_COIN, "2026-300A"));
}

// ---------------------------------------------------------------------------
// SB-004  disabled craft (verified) -> Rule 1, the able craft gives way.
// ---------------------------------------------------------------------------
void sb004() {
    CraftDescriptor b = crossing_b();
    b.can_maneuver = false;
    const PairResult r = run_pair(crossing_a(), b);
    report("SB-004", "disabled craft stands on",
           expect(r, DecidingRule::CAPABILITY, "2026-001A") &&
           !r.v_ab.advisory);
}

// ---------------------------------------------------------------------------
// SB-005  tier conflict (verified CREWED) -> Rule 2, lower tier gives way.
// ---------------------------------------------------------------------------
void sb005() {
    CraftDescriptor b = crossing_b();
    b.tier = Tier::CREWED;
    const PairResult r = run_pair(crossing_a(), b);
    report("SB-005", "crewed stands on over operational",
           expect(r, DecidingRule::TIER, "2026-001A"));
}

// ---------------------------------------------------------------------------
// SB-006  exact tie (vertical head-on, no starboard signal) -> fair coin.
// ---------------------------------------------------------------------------
void sb006() {
    const CraftDescriptor a =
        mk("2026-600A", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
           Vec3(0.0, 0.0, R_LEO), Vec3(V_LEO, 0.0, 0.0), 9.0e4);
    const CraftDescriptor b =
        mk("2026-600B", Tier::OPERATIONAL, true, TrustLevel::VERIFIED,
           Vec3(2.0 * V_LEO * 100.0, 0.0, R_LEO + 300.0),
           Vec3(-V_LEO, 0.0, 0.0), 9.0e4);
    const PairResult r = run_pair(a, b);
    report("SB-006", "exact tie: coin is total",
           expect(r, DecidingRule::FAIR_COIN, "2026-600A") &&
           actionable(r.c_ab));
}

// ---------------------------------------------------------------------------
// SB-007  three-body window: capability verdict, then cascade lock.
//
// PHYSICAL geometry, found by the canonical screener: acme rides a circular
// orbit; hulk (an untrusted derelict) crosses it at t=420 s, orbx crosses at
// t=800 s. Both TCA epochs derive from the shared epoch + screened t_ca, so
// the window ordering is exactly what every participant computes on orbit.
// The pass must (1) hand acme the give-way under Rule 1 against the derelict,
// then (2) cascade acme's GIVE_WAY lock onto the orbx pair — one combined
// burn instead of two.
// ---------------------------------------------------------------------------
void sb007() {
    CraftDescriptor acme = mk("2026-001A", Tier::OPERATIONAL, true,
                              TrustLevel::VERIFIED, Vec3{}, Vec3{}, 9.0e4);
    acme.state = canonicalize(circular(R_LEO, 0.0, 0.0));
    CraftDescriptor hulk = mk("1999-025DEB", Tier::DEORBITING, false,
                              TrustLevel::ASSERTED, Vec3{}, Vec3{},
                              canon::DEFAULT_VAR_M2);
    hulk.state = canonicalize(crossing_against(acme.state, 420.0, 40.0,
                                               35.0 * canon::PI / 180.0));
    CraftDescriptor orbx = mk("2026-044B", Tier::OPERATIONAL, true,
                              TrustLevel::VERIFIED, Vec3{}, Vec3{}, 9.0e4);
    orbx.state = canonicalize(crossing_against(acme.state, 800.0, 150.0,
                                               0.25 * canon::PI));

    OrbitConjunction c_ah, c_ao;
    const bool f_ah = screen_orbits(acme.state, hulk.state, acme.covariance,
                                    hulk.covariance, 20.0,
                                    canon::SCREEN_ARC_MAX_S, &c_ah);
    const bool f_ao = screen_orbits(acme.state, orbx.state, acme.covariance,
                                    orbx.covariance, 20.0,
                                    canon::SCREEN_ARC_MAX_S, &c_ao);

    const CraftDescriptor fleet[3] = {acme, orbx, hulk};
    const auto tca_ms = [](double t_ca) {
        return EPOCH_MS + static_cast<uint64_t>(t_ca * 1000.0);
    };
    WindowEntry win[2];
    win[0] = {0u, 2u, tca_ms(c_ah.conj.t_ca), c_ah.a_tca, c_ah.b_tca};
    win[1] = {0u, 1u, tca_ms(c_ao.conj.t_ca), c_ao.a_tca, c_ao.b_tca};
    WindowVerdict wv[2];
    const uint32_t n = resolve_window(fleet, 3u, win, 2u, wv);
    const bool ok = f_ah && f_ao &&
                    c_ah.conj.detected && c_ao.conj.detected &&
                    (n == 2u) &&
                    (wv[0].give_way_index == 0u) &&
                    (wv[0].deciding_rule == DecidingRule::CAPABILITY) &&
                    !wv[0].escalated && !wv[0].advisory &&
                    (wv[1].give_way_index == 0u) &&
                    (wv[1].deciding_rule == DecidingRule::CASCADE) &&
                    !wv[1].escalated;
    report("SB-007", "3-body window: one-pass cascade", ok);
}

// ---------------------------------------------------------------------------
// SB-008  trust downgrades: unverified claims degrade toward physics.
// ---------------------------------------------------------------------------
void sb008() {
    // (a) A spoofed CREWED tier with an unverifiable signature never reaches
    //     Rule 2: the craft is treated as debris and simply avoided.
    CraftDescriptor b = crossing_b();
    b.tier = Tier::CREWED;
    b.trust = TrustLevel::ASSERTED;
    const PairResult ra = run_pair(crossing_a(), b);
    const bool a_ok = expect(ra, DecidingRule::CAPABILITY, "2026-001A") &&
                      !ra.v_ab.advisory;

    // (b) When BOTH craft are unverified, Rules 1-2 tie out and the decision
    //     falls through to unspoofable geometry (advisory: nobody is trusted
    //     to maneuver, so operators must be alerted).
    CraftDescriptor a2 = crossing_a();
    a2.trust = TrustLevel::ASSERTED;
    const PairResult rb = run_pair(a2, b);
    const bool b_ok = expect(rb, DecidingRule::STARBOARD, "2026-001A") &&
                      rb.v_ab.advisory;

    report("SB-008", "unverified claims fall to physics", a_ok && b_ok);
}

// ---------------------------------------------------------------------------
// SB-009  the Pc gate: same 1100 m miss; covariance decides actionability.
// ---------------------------------------------------------------------------
void sb009() {
    const Vec3 pa(0.0, 0.0, R_LEO), va(V_LEO, 0.0, 0.0);
    const Vec3 pb(V_LEO * 300.0, -V_LEO * 300.0, R_LEO + 1100.0);
    const Vec3 vb(0.0, V_LEO, 0.0);
    const CraftDescriptor tight = mk("2026-900A", Tier::OPERATIONAL, true,
                                     TrustLevel::VERIFIED, pa, va, 5.0e3);
    const CraftDescriptor tightb = mk("2026-900B", Tier::OPERATIONAL, true,
                                      TrustLevel::VERIFIED, pb, vb, 5.0e3);
    const CraftDescriptor fat = mk("2026-900A", Tier::OPERATIONAL, true,
                                   TrustLevel::VERIFIED, pa, va, 3.025e5);
    const CraftDescriptor fatb = mk("2026-900B", Tier::OPERATIONAL, true,
                                    TrustLevel::VERIFIED, pb, vb, 3.025e5);
    const PairResult rt = run_pair(tight, tightb);
    const PairResult rf = run_pair(fat, fatb);
    // Pinned reference values: tight Pc = 8.961e-24, fat Pc = 1.216e-04.
    report("SB-009", "Pc gate: covariance decides action",
           rt.c_ab.detected && !actionable(rt.c_ab) &&
           actionable(rf.c_ab) && rt.agree && rf.agree);
}

// ---------------------------------------------------------------------------
// SB-010  the pinned transcendental: canon_exp bit anchors + accuracy.
// ---------------------------------------------------------------------------
void sb010() {
    const bool anchors =
        (bits_of(canon::canon_exp(-0.5)) == 0x3FE368B2FC6F960Aull) &&
        (bits_of(canon::canon_exp(-2.0)) == 0x3FC152AAA3BF81CCull) &&
        (bits_of(canon::canon_exp(-64.0)) == 0x3A2969D47321E4CCull) &&
        (canon::canon_exp(0.0) == 1.0) && (canon::canon_exp(-800.0) == 0.0);
    bool accurate = true;
    for (int i = 1; i <= 200; ++i) {
        const double x = -0.37 * static_cast<double>(i);
        const double ref = std::exp(x);
        const double err = (canon::canon_exp(x) - ref) / ref;
        if ((err > 1.0e-13) || (err < -1.0e-13)) { accurate = false; }
    }
    report("SB-010", "canon_exp: pinned bits, 1e-13 accuracy",
           anchors && accurate);
}

// ---------------------------------------------------------------------------
// SB-011  Theorem 2: double give-way is safe (displacements oppose).
// ---------------------------------------------------------------------------
void sb011() {
    const CraftDescriptor a = crossing_a();
    const CraftDescriptor b = crossing_b();
    const double hbr = a.hard_body_radius_m + b.hard_body_radius_m;
    const Conjunction ca =
        assess(a.state, b.state, a.covariance, b.covariance, hbr, HORIZON_S);
    const Conjunction cb =
        assess(b.state, a.state, b.covariance, a.covariance, hbr, HORIZON_S);
    // Suppose BOTH craft (wrongly) conclude they must give way.
    const GiveWayPlan pa =
        plan_give_way(a.state, linear_at(a.state, ca.t_ca), ca, true);
    const GiveWayPlan pb =
        plan_give_way(b.state, linear_at(b.state, cb.t_ca), cb, false);
    const Vec3 ua = ca.miss_vector * (-1.0 / ca.miss_distance);
    const Vec3 ub = cb.miss_vector * (-1.0 / cb.miss_distance);
    const bool oppose = ua.dot(ub) < -0.999999;
    report("SB-011", "double give-way still separates",
           pa.required && pb.required && oppose &&
           (pa.displacement_m > 0.0) && (pb.displacement_m > 0.0));
}

// ---------------------------------------------------------------------------
// SB-012  swap-invariance sweep: 4096 pseudo-random geometries (fixed LCG).
//         Empirical pin of Theorem 1: totality + agreement in EVERY case.
//
// The draws are hoisted into named locals BEFORE any constructor call:
// C++ leaves argument evaluation order unspecified, and a side-effecting
// generator inside an argument list produces different populations on
// different compilers (found by tools/bitcheck.sh — gcc and clang really do
// disagree there). Sequencing by declaration makes the sweep well-defined.
// ---------------------------------------------------------------------------
void make_sweep_pair(int i, uint64_t* s, CraftDescriptor* a,
                     CraftDescriptor* b) {
    const auto rnd = [s]() {
        *s = lcg_next(*s);
        return static_cast<double>(*s >> 11) / 9007199254740992.0; // [0,1)
    };
    const auto rv = [&rnd]() { return 2.0 * rnd() - 1.0; };
    char ida[16], idb[16];
    std::snprintf(ida, sizeof(ida), "20%02d-%03dA", i % 27, i % 999);
    std::snprintf(idb, sizeof(idb), "20%02d-%03dB", (i * 7) % 27,
                  (i * 13) % 999);
    const double pax = rv() * 5.0e4, pay = rv() * 5.0e4;
    const double paz = R_LEO + rv() * 5.0e4;
    const double vax = rv() * V_LEO, vay = rv() * V_LEO, vaz = rv() * 100.0;
    const double cva = 1.0e3 + rnd() * 1.0e6;
    *a = mk(ida, static_cast<Tier>(i % 4), (i % 3) != 0,
            static_cast<TrustLevel>(i % 3), Vec3(pax, pay, paz),
            Vec3(vax, vay, vaz), cva);
    const double pbx = rv() * 5.0e4, pby = rv() * 5.0e4;
    const double pbz = R_LEO + rv() * 5.0e4;
    const double vbx = rv() * V_LEO, vby = rv() * V_LEO, vbz = rv() * 100.0;
    const double cvb = 1.0e3 + rnd() * 1.0e6;
    *b = mk(idb, static_cast<Tier>((i / 4) % 4), (i % 5) != 0,
            static_cast<TrustLevel>((i / 3) % 3), Vec3(pbx, pby, pbz),
            Vec3(vbx, vby, vbz), cvb);
}

void sb012() {
    uint64_t s = 0x5EEDull;
    bool ok = true;
    for (int i = 0; i < 4096 && ok; ++i) {
        CraftDescriptor a, b;
        make_sweep_pair(i, &s, &a, &b);
        const PairResult r = run_pair(a, b);
        const bool named_one =
            id_equal(r.v_ab.give_way_id, a.object_id) ||
            id_equal(r.v_ab.give_way_id, b.object_id);
        ok = r.agree && named_one;
    }
    report("SB-012", "4096-geometry swap-invariance sweep", ok);
}

// ---------------------------------------------------------------------------
// SB-013  end-to-end canonical search: two crossing circular orbits (the
//         demo's Act I geometry), conjunction FOUND by the orbital screener,
//         verdict at the encounter. TCA/Pc are SB-C2 bit anchors.
// ---------------------------------------------------------------------------
void sb013() {
    const double R = 6.93e6;
    CraftDescriptor a = mk("2026-001A", Tier::OPERATIONAL, true,
                           TrustLevel::VERIFIED, Vec3{}, Vec3{}, 9.0e4);
    a.state = canonicalize(circular(R, 0.0, 0.0));
    CraftDescriptor b = mk("2026-044B", Tier::OPERATIONAL, true,
                           TrustLevel::VERIFIED, Vec3{}, Vec3{}, 9.0e4);
    b.state = canonicalize(crossing_against(a.state, 800.0, 150.0,
                                            0.25 * canon::PI));

    OrbitConjunction fwd, rev;
    const bool fa = screen_orbits(a.state, b.state, a.covariance,
                                  b.covariance, 20.0, canon::SCREEN_ARC_MAX_S,
                                  &fwd);
    const bool fb = screen_orbits(b.state, a.state, b.covariance,
                                  a.covariance, 20.0, canon::SCREEN_ARC_MAX_S,
                                  &rev);
    const bool sym = fa && fb &&
                     (bits_of(fwd.conj.t_ca) == bits_of(rev.conj.t_ca)) &&
                     (bits_of(fwd.conj.pc) == bits_of(rev.conj.pc));

    CraftDescriptor a_tca = a;
    a_tca.state = fwd.a_tca;
    CraftDescriptor b_tca = b;
    b_tca.state = fwd.b_tca;
    const GiveWayVerdict v = evaluate(a_tca, b_tca, EPOCH_MS);
    const GiveWayVerdict w = evaluate(b_tca, a_tca, EPOCH_MS);

    const bool anchors =
        (bits_of(fwd.conj.t_ca) == SB013_TCA_BITS) &&
        (bits_of(fwd.conj.pc) == SB013_PC_BITS);
    const bool miss_ok = (fwd.conj.miss_distance > 148.0) &&
                         (fwd.conj.miss_distance < 152.0) &&
                         (fwd.conj.t_ca > 799.0) && (fwd.conj.t_ca < 801.0);
    report("SB-013", "J2 orbital search: crossing found + bits",
           sym && miss_ok && actionable(fwd.conj) && anchors &&
           (v.deciding_rule == DecidingRule::STARBOARD) &&
           id_equal(v.give_way_id, w.give_way_id) &&
           (v.deciding_rule == w.deciding_rule));
}

// ---------------------------------------------------------------------------
// SB-014  canonical trig + J2 propagator: canon_sincos bit anchors + 1e-13
//         accuracy; propagator step-refinement convergence and forward/
//         backward invertibility; presence of the J2 nodal signature.
// ---------------------------------------------------------------------------
void sb014() {
    const bool anchors =
        (bits_of(canon::canon_sin(1.0)) == SB014_SIN1_BITS) &&
        (bits_of(canon::canon_cos(1.0)) == SB014_COS1_BITS) &&
        (bits_of(canon::canon_sin(-18.849)) == SB014_SINM_BITS);
    bool accurate = true;
    for (int i = -640; i <= 640; ++i) {
        const double x = 0.1 * static_cast<double>(i);
        double s, c;
        canon::canon_sincos(x, &s, &c);
        if ((std::fabs(s - std::sin(x)) > 1.0e-13) ||
            (std::fabs(c - std::cos(x)) > 1.0e-13)) { accurate = false; }
    }
    // Forward/backward invertibility: propagate out the full arc and back,
    // recover the start to < 1 mm (RK4 is time-symmetric; this exercises the
    // signed-step decomposition).
    const CraftState s0 = canonicalize(circular(6.93e6, 0.9, 1.1));
    const CraftState out = propagate(s0, canon::SCREEN_ARC_MAX_S);
    const CraftState back = propagate(out, -canon::SCREEN_ARC_MAX_S);
    const bool invertible = (back.position - s0.position).norm() < 1.0e-3;
    // J2 signature: over the arc the orbit-normal direction rotates (nodal
    // regression) — a two-body propagator would leave it fixed. Confirms J2
    // is actually in the canonical dynamics.
    const Vec3 h0 = s0.position.cross(s0.velocity).normalized();
    const Vec3 h1 = out.position.cross(out.velocity).normalized();
    const bool j2_present = (h0 - h1).norm() > 1.0e-7;
    // Return-to-slot (SPEC §6.5): the quarter-period two-impulse transfer
    // must land back on-slot, on-velocity in CW dynamics — verified by
    // pushing the planned burns through the CW state transition itself.
    const double n_ref = mean_motion(s0);
    HillState off;
    off.r = Vec3(120.0, -340.0, 60.0);
    off.v = Vec3(0.10, -0.20, 0.05);
    const ReturnPlan rp = plan_return(off, n_ref);
    HillState dep = off;
    dep.v = dep.v + rp.dv1_hill;
    HillState arr = cw_propagate(dep, n_ref, rp.transfer_s);
    arr.v = arr.v + rp.dv2_hill;
    const bool return_ok = (arr.r.norm() < 1.0e-6) &&
                           (arr.v.norm() < 1.0e-9) && (rp.total_dv > 0.0);
    report("SB-014", "canon trig + J2 propagator + CW return",
           anchors && accurate && invertible && j2_present && return_ok);
}

// ---------------------------------------------------------------------------
// SB-015  SHA-512 against the FIPS 180-2 example vectors.
// ---------------------------------------------------------------------------
uint32_t hex_decode(const char* hex, uint8_t* out, uint32_t max) {
    uint32_t n = 0u;
    while ((hex[2 * n] != '\0') && (n < max)) {
        const auto nib = [](char c) -> uint8_t {
            if ((c >= '0') && (c <= '9')) { return static_cast<uint8_t>(c - '0'); }
            if ((c >= 'a') && (c <= 'f')) { return static_cast<uint8_t>(c - 'a' + 10); }
            return static_cast<uint8_t>(c - 'A' + 10);
        };
        out[n] = static_cast<uint8_t>((nib(hex[2 * n]) << 4) |
                                      nib(hex[2 * n + 1]));
        ++n;
    }
    return n;
}

void sb015() {
    struct V { const char* msg; const char* digest; };
    const V vs[3] = {
        {"abc",
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
         "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
        {"",
         "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
         "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
         "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
         "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"},
    };
    bool ok = true;
    for (const V& v : vs) {
        uint8_t want[64], got[64];
        (void)hex_decode(v.digest, want, 64u);
        uint64_t len = 0u;
        while (v.msg[len] != '\0') { ++len; }
        crypto::sha512(reinterpret_cast<const uint8_t*>(v.msg), len, got);
        if (std::memcmp(want, got, 64u) != 0) { ok = false; }
    }
    report("SB-015", "SHA-512: FIPS 180-2 vectors", ok);
}

// ---------------------------------------------------------------------------
// SB-016  Ed25519 verification against RFC 8032 §7.1, plus rejections.
// ---------------------------------------------------------------------------
void sb016() {
    struct V { const char* pub; const char* msg; const char* sig; };
    const V vs[3] = {
        {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
         "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
         "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
         "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
         "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
         "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
         "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    bool ok = true;
    for (const V& v : vs) {
        uint8_t pub[32], sig[64], msg[8];
        (void)hex_decode(v.pub, pub, 32u);
        (void)hex_decode(v.sig, sig, 64u);
        const uint32_t mlen = hex_decode(v.msg, msg, 8u);
        if (!crypto::ed25519_verify(sig, msg, mlen, pub)) { ok = false; }
        uint8_t bad[64];
        std::memcpy(bad, sig, 64u);
        bad[0] ^= 1u;
        if (crypto::ed25519_verify(bad, msg, mlen, pub)) { ok = false; }
        std::memcpy(bad, sig, 64u);
        bad[63] ^= 0x40u;
        if (crypto::ed25519_verify(bad, msg, mlen, pub)) { ok = false; }
        uint8_t badpub[32];
        std::memcpy(badpub, pub, 32u);
        badpub[7] ^= 4u;
        if (crypto::ed25519_verify(sig, msg, mlen, badpub)) { ok = false; }
    }
    // Tampered message must fail.
    {
        uint8_t pub[32], sig[64];
        (void)hex_decode(vs[2].pub, pub, 32u);
        (void)hex_decode(vs[2].sig, sig, 64u);
        const uint8_t wrong[2] = {0xAFu, 0x83u};
        if (crypto::ed25519_verify(sig, wrong, 2u, pub)) { ok = false; }
    }
    // All-zero and small-order public keys, and non-canonical S (== L),
    // must be rejected. The identity-point key (0x01, 0...) is the
    // repudiation attack: anyone can "sign" under it.
    {
        uint8_t zero[32] = {0};
        uint8_t sig[64] = {0};
        if (crypto::ed25519_verify(sig, nullptr, 0u, zero)) { ok = false; }
        uint8_t ident[32] = {0};
        ident[0] = 1u;
        // Any (R, S) with S < L "verifies" under the identity key when
        // R = [S]B; use S = 0, R = identity encoding — must be rejected.
        uint8_t forge[64] = {0};
        forge[0] = 1u;
        if (crypto::ed25519_verify(forge, nullptr, 0u, ident)) { ok = false; }
        uint8_t pub[32];
        (void)hex_decode(vs[0].pub, pub, 32u);
        uint8_t sl[64] = {0};
        const uint8_t lbytes[32] = {
            0xEDu, 0xD3u, 0xF5u, 0x5Cu, 0x1Au, 0x63u, 0x12u, 0x58u,
            0xD6u, 0x9Cu, 0xF7u, 0xA2u, 0xDEu, 0xF9u, 0xDEu, 0x14u,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10u};
        std::memcpy(&sl[32], lbytes, 32u);
        if (crypto::ed25519_verify(sl, nullptr, 0u, pub)) { ok = false; }
    }
    report("SB-016", "Ed25519: RFC 8032 verify + rejects", ok);
}

// ---------------------------------------------------------------------------
// SB-017  the registry pipeline (fixtures signed by libsodium — an
//         INDEPENDENT implementation of REGISTRY.md).
// ---------------------------------------------------------------------------
void sb017() {
    using registry::establish_trust;
    const auto trust = [](const CapabilityAssertion& a, uint32_t entry_i,
                          const uint8_t reg_pub[32]) {
        return establish_trust(a, fixtures::ENTRIES[entry_i],
                               fixtures::PROOFS[entry_i], fixtures::SNAPSHOT,
                               fixtures::REVOKED_IDS, 1u, reg_pub,
                               fixtures::NOW_MS);
    };
    const bool ok_verified =
        trust(fixtures::ASSERT_OK, 0u, fixtures::REG_PUB) ==
        TrustLevel::VERIFIED;
    const bool revoked_downgraded =
        trust(fixtures::ASSERT_REVOKED, 2u, fixtures::REG_PUB) ==
        TrustLevel::ASSERTED;
    const bool expired_downgraded =
        trust(fixtures::ASSERT_EXPIRED, 1u, fixtures::REG_PUB) ==
        TrustLevel::ASSERTED;
    CapabilityAssertion tampered = fixtures::ASSERT_OK;
    tampered.can_maneuver = !tampered.can_maneuver;
    const bool tamper_downgraded =
        trust(tampered, 0u, fixtures::REG_PUB) == TrustLevel::ASSERTED;
    uint8_t bad_reg[32];
    std::memcpy(bad_reg, fixtures::REG_PUB, 32u);
    bad_reg[3] ^= 8u;
    const bool badreg_downgraded =
        trust(fixtures::ASSERT_OK, 0u, bad_reg) == TrustLevel::ASSERTED;
    // Wrong entry for the assertion (key binding mismatch) must fail too.
    const bool wrong_entry_downgraded =
        establish_trust(fixtures::ASSERT_OK, fixtures::ENTRIES[1],
                        fixtures::PROOFS[1], fixtures::SNAPSHOT,
                        fixtures::REVOKED_IDS, 1u, fixtures::REG_PUB,
                        fixtures::NOW_MS) == TrustLevel::ASSERTED;
    report("SB-017", "registry pipeline: verify + downgrades",
           ok_verified && revoked_downgraded && expired_downgraded &&
           tamper_downgraded && badreg_downgraded && wrong_entry_downgraded);
}

// ---------------------------------------------------------------------------
// SB-019  snapshot freshness: a validly-signed but STALE snapshot is
//         rejected (else its stale revocation list would un-revoke a key),
//         and a future-dated snapshot is rejected (rollback/clock skew).
//         Uses the long-window assertion so this isolates SNAPSHOT age from
//         assertion expiry.
// ---------------------------------------------------------------------------
void sb019() {
    using registry::establish_trust;
    const auto trust_at = [](const CapabilityAssertion& a, uint64_t now,
                             uint64_t max_age) {
        return establish_trust(a, fixtures::ENTRIES[0], fixtures::PROOFS[0],
                               fixtures::SNAPSHOT, fixtures::REVOKED_IDS, 1u,
                               fixtures::REG_PUB, now, max_age);
    };
    const uint64_t day = 86400000ull;
    // Fresh: snapshot 1 day old, 30-day bound, assertion still valid.
    const bool fresh_ok =
        trust_at(fixtures::ASSERT_LONG, fixtures::NOW_MS,
                 registry::SNAPSHOT_MAX_AGE_MS) == TrustLevel::VERIFIED;
    // Stale: same snapshot evaluated 400 days later — assertion still in
    // window, but the snapshot is far past the 30-day bound.
    const bool stale_rejected =
        trust_at(fixtures::ASSERT_LONG, fixtures::SNAPSHOT_EPOCH_MS + 400 * day,
                 registry::SNAPSHOT_MAX_AGE_MS) == TrustLevel::ASSERTED;
    // Future-dated snapshot (now before its epoch) rejected.
    const bool future_rejected =
        trust_at(fixtures::ASSERT_LONG, fixtures::SNAPSHOT_EPOCH_MS - day,
                 registry::SNAPSHOT_MAX_AGE_MS) == TrustLevel::ASSERTED;
    // Exactly at the bound is still accepted; one ms past is not.
    const bool boundary_ok =
        (trust_at(fixtures::ASSERT_LONG,
                  fixtures::SNAPSHOT_EPOCH_MS + registry::SNAPSHOT_MAX_AGE_MS,
                  registry::SNAPSHOT_MAX_AGE_MS) == TrustLevel::VERIFIED) &&
        (trust_at(fixtures::ASSERT_LONG,
                  fixtures::SNAPSHOT_EPOCH_MS + registry::SNAPSHOT_MAX_AGE_MS + 1,
                  registry::SNAPSHOT_MAX_AGE_MS) == TrustLevel::ASSERTED);
    report("SB-019", "snapshot freshness: stale/future rejected",
           fresh_ok && stale_rejected && future_rejected && boundary_ok);
}

// ---------------------------------------------------------------------------
// SB-020  broad-phase recall = 1.0: on a seeded constellation, every pair the
//         pairwise search detects is present in the radial-band candidate set
//         (the unconditional superset property, SPEC §4.8), and it prunes.
// ---------------------------------------------------------------------------
void sb020() {
    const uint32_t N = 220u;
    CraftState objs[260];
    uint64_t s = 0xB0A2Dull;
    const auto rnd = [&s]() {
        s = lcg_next(s);
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    };
    const double incs[5] = {0.0, 28.0, 53.0, 70.0, 98.0};
    uint32_t m = 0u;
    for (uint32_t i = 0u; i < N; ++i) {
        const double r = canon::R_EARTH_M + 400.0e3 + 800.0e3 * rnd();
        objs[m++] = circular(r, incs[i % 5] * canon::PI / 180.0,
                             2.0 * canon::PI * rnd());
    }
    // Seed close pairs: clone with a cross-track offset inside R_SCREEN.
    for (uint32_t k = 0u; k < 30u; ++k) {
        const uint32_t i = static_cast<uint32_t>(rnd() * N);
        CraftState c = objs[i];
        const Vec3 ch = c.position.cross(c.velocity).normalized();
        c.position = c.position + ch * 150.0;
        objs[m++] = c;
    }
    const uint32_t total = m;

    // Brute-force truth.
    uint32_t truth = 0u, missed = 0u;
    broadphase::Band bands[260];
    uint32_t active[260];
    broadphase::Pair out[260 * 260 / 2];
    bool ovf = false;
    const uint32_t nc = broadphase::radial_band_pairs(
        objs, total, bands, active, out, sizeof(out) / sizeof(out[0]), &ovf);
    for (uint32_t i = 0u; i < total; ++i) {
        for (uint32_t j = i + 1u; j < total; ++j) {
            OrbitConjunction oc;
            if (screen_orbits(objs[i], objs[j], Sym33::diagonal(1.0e4),
                              Sym33::diagonal(1.0e4), 20.0,
                              canon::SCREEN_ARC_MAX_S, &oc)) {
                ++truth;
                bool found = false;
                for (uint32_t k = 0u; k < nc; ++k) {
                    if (out[k].i == i && out[k].j == j) { found = true; break; }
                }
                if (!found) { ++missed; }
            }
        }
    }
    const double n2 = 0.5 * static_cast<double>(total) *
                      static_cast<double>(total - 1u);
    report("SB-020", "broad-phase recall=1.0 + pruning",
           !ovf && (missed == 0u) && (truth > 0u) &&
           (static_cast<double>(nc) < 0.75 * n2));
}

// ---------------------------------------------------------------------------
// SB-018  Merkle log: C++ root reproduces the (Python-signed) snapshot root;
//         all inclusion proofs verify; corruption fails.
// ---------------------------------------------------------------------------
void sb018() {
    using namespace registry;
    uint8_t leaves[5][HASH_BYTES];
    for (uint32_t i = 0u; i < 5u; ++i) {
        leaf_hash(fixtures::ENTRIES[i], leaves[i]);
    }
    uint8_t nodes[5][HASH_BYTES];
    std::memcpy(nodes, leaves, sizeof(nodes));
    uint8_t root[HASH_BYTES];
    merkle_root(nodes, 5u, root);
    const bool cross_impl =
        (std::memcmp(root, fixtures::SNAPSHOT.root, HASH_BYTES) == 0);

    bool proofs_ok = true;
    for (uint32_t i = 0u; i < 5u; ++i) {
        std::memcpy(nodes, leaves, sizeof(nodes));
        InclusionProof p;
        if (!merkle_proof(nodes, 5u, i, &p)) { proofs_ok = false; continue; }
        if (!verify_inclusion(leaves[i], p, 5ull, root)) { proofs_ok = false; }
        if (!verify_inclusion(leaves[i], fixtures::PROOFS[i], 5ull,
                              fixtures::SNAPSHOT.root)) {
            proofs_ok = false;
        }
        if (p.depth > 0u) {
            p.siblings[0][0] ^= 1u;
            if (verify_inclusion(leaves[i], p, 5ull, root)) {
                proofs_ok = false;   // corrupted proof must fail
            }
        }
        if (verify_inclusion(leaves[i], fixtures::PROOFS[i],
                             5ull, fixtures::SNAPSHOT.revocation_root)) {
            proofs_ok = false;       // wrong root must fail
        }
    }
    report("SB-018", "Merkle log: cross-impl root + proofs",
           cross_impl && proofs_ok);
}

// ---------------------------------------------------------------------------
// --bits: the SB-C2 bit transcript. Emits the bit patterns of every pinned
// quantity plus FNV-1a fold digests over dense sweeps (thousands of
// intermediate values each), so ANY arithmetic divergence between two
// builds/compilers/machines — not just at the anchors — changes the output.
// tools/bitcheck.sh diffs this against conformance/expected_bits.txt across
// the toolchain matrix. The transcript is normative for SB-C2.
// ---------------------------------------------------------------------------
struct Fold {
    uint64_t h = 14695981039346656037ull;
    void add(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFFull;
            h *= 1099511628211ull;
        }
    }
    void add(double d) { add(bits_of(d)); }
    void add(const Vec3& v) { add(v.x); add(v.y); add(v.z); }
};

void emit_bits() {
    std::printf("starboard-bit-transcript v1 ruleset %08X\n", canon::RULESET_ID);

    // Pinned transcendentals: explicit anchors + dense fold.
    std::printf("exp(-0.5)      %016llX\n",
        static_cast<unsigned long long>(bits_of(canon::canon_exp(-0.5))));
    std::printf("sin(1.0)       %016llX\n",
        static_cast<unsigned long long>(bits_of(canon::canon_sin(1.0))));
    std::printf("cos(1.0)       %016llX\n",
        static_cast<unsigned long long>(bits_of(canon::canon_cos(1.0))));
    Fold ft;
    for (int i = -4096; i <= 4096; ++i) {
        const double x = 0.017 * static_cast<double>(i);
        double s, c;
        canon::canon_sincos(x, &s, &c);
        ft.add(s);
        ft.add(c);
        if (x <= 0.0) { ft.add(canon::canon_exp(x)); }
    }
    std::printf("transcendental %016llX\n",
                static_cast<unsigned long long>(ft.h));

    // Canonical J2 propagator: fold the SB-013 pair across a dense time grid.
    const double R = 6.93e6;
    const CraftState ka = canonicalize(circular(R, 0.0, 0.0));
    const CraftState kb =
        canonicalize(crossing_against(ka, 800.0, 150.0, 0.25 * canon::PI));
    Fold fk;
    for (int i = 0; i <= 256; ++i) {
        const double t = 4.0 * static_cast<double>(i);
        const CraftState sa = propagate(ka, t);
        const CraftState sb = propagate(kb, t);
        fk.add(sa.position); fk.add(sa.velocity);
        fk.add(sb.position); fk.add(sb.velocity);
    }
    std::printf("ephemeris      %016llX\n",
                static_cast<unsigned long long>(fk.h));

    // End-to-end search anchors.
    OrbitConjunction oc;
    Sym33 cov = Sym33::diagonal(9.0e4);
    (void)screen_orbits(ka, kb, cov, cov, 20.0, canon::SCREEN_ARC_MAX_S, &oc);
    std::printf("search.tca     %016llX\n",
        static_cast<unsigned long long>(bits_of(oc.conj.t_ca)));
    std::printf("search.miss    %016llX\n",
        static_cast<unsigned long long>(bits_of(oc.conj.miss_distance)));
    std::printf("search.pc      %016llX\n",
        static_cast<unsigned long long>(bits_of(oc.conj.pc)));

    // Pc kernel: fold a miss x sigma x hbr parameter sweep.
    Fold fp;
    for (int im = 1; im <= 20; ++im) {
        for (int is = 1; is <= 20; ++is) {
            Conjunction c;
            c.detected = true;
            c.t_ca = 100.0;
            c.miss_vector = Vec3(0.0, 0.0, 128.0 * static_cast<double>(im));
            c.miss_distance = c.miss_vector.norm();
            c.rel_velocity = Vec3(7000.0, -3000.0, 0.0);
            c.pc = 0.0;
            const Sym33 cc =
                Sym33::diagonal(1024.0 * static_cast<double>(is * is));
            fp.add(starboard_pc(c, cc, 5.0 + 2.0 * static_cast<double>(im)));
        }
    }
    std::printf("pc-kernel      %016llX\n",
                static_cast<unsigned long long>(fp.h));

    // Verdict sweep: fold the full SB-012 LCG population's verdicts.
    Fold fv;
    uint64_t s = 0x5EEDull;
    for (int i = 0; i < 4096; ++i) {
        CraftDescriptor a, b;
        make_sweep_pair(i, &s, &a, &b);
        const PairResult r = run_pair(a, b);
        fv.add(r.v_ab.conjunction_id);
        fv.add(static_cast<uint64_t>(r.v_ab.deciding_rule));
        fv.add(r.c_ab.pc);
        for (int j = 0; j < 12; ++j) {
            fv.add(static_cast<uint64_t>(
                static_cast<unsigned char>(r.v_ab.give_way_id.c[j])));
        }
    }
    std::printf("verdict-sweep  %016llX\n",
                static_cast<unsigned long long>(fv.h));

    // Registry stack: fold the Merkle root and a SHA-512 chain (pure integer
    // arithmetic — divergence here would mean a broken toolchain, but the
    // transcript should cover the whole stack).
    Fold fr;
    uint8_t nodes[5][registry::HASH_BYTES];
    for (uint32_t i = 0u; i < 5u; ++i) {
        registry::leaf_hash(fixtures::ENTRIES[i], nodes[i]);
    }
    uint8_t rroot[registry::HASH_BYTES];
    registry::merkle_root(nodes, 5u, rroot);
    for (uint32_t i = 0u; i < registry::HASH_BYTES; i += 8u) {
        uint64_t w = 0u;
        for (uint32_t j = 0u; j < 8u; ++j) {
            w |= static_cast<uint64_t>(rroot[i + j]) << (8u * j);
        }
        fr.add(w);
    }
    std::printf("registry       %016llX\n",
                static_cast<unsigned long long>(fr.h));
}

} // namespace

int main(int argc, char** argv) {
    if (!canon::arithmetic_profile_ok()) {
        std::fprintf(stderr, "FATAL: FP contraction detected (SPEC §3.5) — "
                             "this build cannot produce conformant "
                             "verdicts.\n");
        return 2;
    }
    if ((argc > 1) && (std::strcmp(argv[1], "--bits") == 0)) {
        emit_bits();
        return 0;
    }
    std::printf("Starboard conformance suite  (ruleset SB/1, id 0x%08X)\n\n",
                canon::RULESET_ID);
    sb001();
    sb002();
    sb003();
    sb004();
    sb005();
    sb006();
    sb007();
    sb008();
    sb009();
    sb010();
    sb011();
    sb012();
    sb013();
    sb014();
    sb015();
    sb016();
    sb017();
    sb018();
    sb019();
    sb020();
    std::printf("\n%s (%d failure%s)\n",
                (g_failures == 0) ? "SB-C1 + SB-C2 vectors: ALL PASS"
                                  : "CONFORMANCE FAILED",
                g_failures, (g_failures == 1) ? "" : "s");
    return (g_failures == 0) ? 0 : 1;
}
