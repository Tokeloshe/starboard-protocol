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

#include <cstdio>
#include <cstring>

using namespace starboard;

namespace {

int g_failures = 0;

uint64_t bits_of(double x) {
    uint64_t u;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

void report(const char* id, const char* name, bool pass) {
    std::printf("  %-8s %-38s %s\n", id, name, pass ? "PASS" : "FAIL");
    if (!pass) { ++g_failures; }
}

// --- shared scenario scaffolding -------------------------------------------
constexpr double R_LEO = 6.93e6;
constexpr double V_LEO = 7600.0;
constexpr double HORIZON_S = 3.0 * 5700.0;
constexpr uint64_t EPOCH_MS = 1401000000000ull;

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

PairResult run_pair(const CraftDescriptor& a, const CraftDescriptor& b) {
    PairResult r;
    const double hbr = a.hard_body_radius_m + b.hard_body_radius_m;
    r.c_ab = assess(a.state, b.state, a.covariance, b.covariance, hbr,
                    HORIZON_S);
    Conjunction c_ba = assess(b.state, a.state, b.covariance, a.covariance,
                              hbr, HORIZON_S);
    r.v_ab = evaluate(a, b, r.c_ab, EPOCH_MS);
    r.v_ba = evaluate(b, a, c_ba, EPOCH_MS);
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
    const bool pc_bits_ok = (bits_of(r.c_ab.pc) == 0x3F5117B3E92585E2ull);
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
// ---------------------------------------------------------------------------
void sb007() {
    const CraftDescriptor acme = crossing_a();
    const CraftDescriptor orbx = crossing_b();
    CraftDescriptor hulk =
        mk("1999-025DEB", Tier::DEORBITING, false, TrustLevel::ASSERTED,
           Vec3(-V_LEO * 900.0, 60.0, R_LEO + 40.0),
           Vec3(V_LEO * 1.01, 0.0, 0.0), canon::DEFAULT_VAR_M2);

    const CraftDescriptor fleet[3] = {acme, orbx, hulk};
    const double hbr = 2.0 * canon::DEFAULT_HBR_M;
    WindowEntry win[2];
    win[0] = {0u, 2u,
              assess(acme.state, hulk.state, acme.covariance, hulk.covariance,
                     hbr, HORIZON_S),
              EPOCH_MS - 1800000ull};
    win[1] = {0u, 1u,
              assess(acme.state, orbx.state, acme.covariance, orbx.covariance,
                     hbr, HORIZON_S),
              EPOCH_MS};
    WindowVerdict wv[2];
    const uint32_t n = resolve_window(fleet, 3u, win, 2u, wv);
    const bool ok = (n == 2u) &&
                    (wv[0].give_way_index == 0u) &&
                    (wv[0].deciding_rule == DecidingRule::CAPABILITY) &&
                    !wv[0].escalated &&
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
    const GiveWayPlan pa = plan_give_way(a.state, ca, true);
    const GiveWayPlan pb = plan_give_way(b.state, cb, false);
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
// ---------------------------------------------------------------------------
void sb012() {
    uint64_t s = 0x5EEDull;
    const auto rnd = [&s]() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) / 9007199254740992.0; // [0,1)
    };
    bool ok = true;
    for (int i = 0; i < 4096 && ok; ++i) {
        char ida[16], idb[16];
        std::snprintf(ida, sizeof(ida), "20%02d-%03dA", i % 27, i % 999);
        std::snprintf(idb, sizeof(idb), "20%02d-%03dB", (i * 7) % 27,
                      (i * 13) % 999);
        const auto rv = [&rnd]() { return 2.0 * rnd() - 1.0; };
        const CraftDescriptor a = mk(
            ida, static_cast<Tier>(i % 4), (i % 3) != 0,
            static_cast<TrustLevel>(i % 3),
            Vec3(rv() * 5.0e4, rv() * 5.0e4, R_LEO + rv() * 5.0e4),
            Vec3(rv() * V_LEO, rv() * V_LEO, rv() * 100.0),
            1.0e3 + rnd() * 1.0e6);
        const CraftDescriptor b = mk(
            idb, static_cast<Tier>((i / 4) % 4), (i % 5) != 0,
            static_cast<TrustLevel>((i / 3) % 3),
            Vec3(rv() * 5.0e4, rv() * 5.0e4, R_LEO + rv() * 5.0e4),
            Vec3(rv() * V_LEO, rv() * V_LEO, rv() * 100.0),
            1.0e3 + rnd() * 1.0e6);
        const PairResult r = run_pair(a, b);
        const bool named_one =
            id_equal(r.v_ab.give_way_id, a.object_id) ||
            id_equal(r.v_ab.give_way_id, b.object_id);
        ok = r.agree && named_one;
    }
    report("SB-012", "4096-geometry swap-invariance sweep", ok);
}

} // namespace

int main() {
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
    std::printf("\n%s (%d failure%s)\n",
                (g_failures == 0) ? "SB-C1 + SB-C2 vectors: ALL PASS"
                                  : "CONFORMANCE FAILED",
                g_failures, (g_failures == 1) ? "" : "s");
    return (g_failures == 0) ? 0 : 1;
}
