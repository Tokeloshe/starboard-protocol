// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.

// ============================================================================
// Starboard demo — the Rules of the Orbital Road, live.
//
// Every state in this demo is PHYSICAL: real circular orbits, propagated
// with the canonical two-body ephemeris. Nothing is placed by hand at the
// encounter — the conjunctions are FOUND by the canonical search.
//
// Act I : two rival-operator satellites on crossing orbits (45 deg mutual
//         inclination, 150 m radial offset at the common node). Zero
//         communication: both find the same conjunction, compute identical
//         Pc bits and the identical give-way verdict; the give-way craft
//         flies a minimum-dv Hill-frame burn and flocks back to its slot.
// Act II: a derelict on a third crossing orbit joins the window; the
//         multi-body resolver settles all pairs in one deterministic pass.
//
//   Build:  cmake -B build && cmake --build build
//   Run:    ./build/starboard_crossing_demo
// ============================================================================

#include "starboard/starboard.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace starboard;

namespace {

const char* rule_str(DecidingRule r) {
    switch (r) {
        case DecidingRule::CAPABILITY: return "Rule 1 - capability";
        case DecidingRule::TIER:       return "Rule 2 - tier";
        case DecidingRule::OVERTAKING: return "Rule 3 - overtaking";
        case DecidingRule::STARBOARD:  return "Rule 4 - starboard";
        case DecidingRule::FAIR_COIN:  return "Rule 5 - fair coin";
        case DecidingRule::CASCADE:    return "Cascade  - window lock";
    }
    return "?";
}

uint64_t bits_of(double x) {
    uint64_t u;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

void stamp(double t_rel_s, const char* msg) {
    const double a = (t_rel_s < 0.0) ? -t_rel_s : t_rel_s;
    std::printf(" T%c%03d:%02d  %s\n", (t_rel_s < 0.0) ? '-' : '+',
                static_cast<int>(a) / 60, static_cast<int>(a) % 60, msg);
}

// A craft on a circular orbit of radius r, in the plane produced by rotating
// the equatorial plane by `incl` about the +x axis, at phase angle theta
// (theta = 0 is the ascending node on +x).
CraftState circular_state(double r, double incl, double theta) {
    const double v = std::sqrt(canon::MU_EARTH_M3S2 / r);
    const double ct = std::cos(theta), st = std::sin(theta);
    const double ci = std::cos(incl), si = std::sin(incl);
    CraftState s;
    s.position = Vec3(r * ct, r * st * ci, r * st * si);
    s.velocity = Vec3(-v * st, v * ct * ci, v * ct * si);
    return s;
}

} // namespace

int main() {
    std::printf("=============================================================\n");
    std::printf(" STARBOARD - the Rules of the Orbital Road, live\n");
    std::printf("=============================================================\n\n");

    // ------------------------------------------------------------------
    // Physical setup. The shared input is a conjunction data message (CDM):
    // both craft's states + covariances at a common reference epoch T0, near
    // the encounter. Phases are set so the crossing falls ~15 min out, inside
    // the bounded screening arc where the canonical J2 propagator is accurate
    // to ~1 m. (Longer lead times re-screen against a fresh CDM.)
    // ------------------------------------------------------------------
    const double R = 6.93e6;                                   // ~552 km alt
    const double n_ref = std::sqrt(canon::MU_EARTH_M3S2 / (R * R * R));
    const double T_CROSS = 900.0;                              // s, Act I TCA
    const uint64_t T0_MS = 1401000000000ull;                   // CDM epoch
    const double ARC = canon::SCREEN_ARC_MAX_S;                // bounded arc

    // ACME-1: equatorial, reaches the +x node at T_CROSS.
    CraftDescriptor acme;
    acme.object_id = make_object_id("2026-001A");
    acme.tier = Tier::OPERATIONAL;
    acme.state = circular_state(R, 0.0, -n_ref * T_CROSS);
    acme.covariance = Sym33::diagonal(9.0e4);                  // sigma 300 m

    // ORBX-9: crosses ACME-1 at T_CROSS, 150 m above, on a 45 deg-tilted
    // track — a genuine crossing. Built at the crossing instant against
    // ACME's J2-propagated state, then back-propagated to the CDM epoch, so
    // the 150 m miss holds under the canonical (J2) propagator.
    CraftDescriptor orbx;
    orbx.object_id = make_object_id("2026-044B");
    orbx.tier = Tier::OPERATIONAL;
    orbx.covariance = Sym33::diagonal(9.0e4);
    {
        const CraftState a_cross = propagate(canonicalize(acme.state), T_CROSS);
        const Vec3 rhat = a_cross.position.normalized();
        const Vec3 vhat = a_cross.velocity.normalized();
        const Vec3 chat = rhat.cross(vhat).normalized();
        const double r_o = a_cross.position.norm() + 150.0;
        const double v_o = std::sqrt(canon::MU_EARTH_M3S2 / r_o);
        const double tilt = 0.25 * canon::PI;                 // 45 deg
        CraftState orbx_cross;
        orbx_cross.position = rhat * r_o;
        orbx_cross.velocity =
            (vhat * std::cos(tilt) + chat * std::sin(tilt)) * v_o;
        orbx.state = propagate(orbx_cross, -T_CROSS);
    }

    std::printf(" Act I - two rivals, zero communication\n");
    std::printf("   ACME-1 [2026-001A]  ACME Space    equatorial, 552 km\n");
    std::printf("   ORBX-9 [2026-044B]  OrbX (rival)  45 deg incl, +150 m\n\n");

    // --- Layer 0: each craft runs the canonical conjunction search ---
    const double hbr = acme.hard_body_radius_m + orbx.hard_body_radius_m;
    OrbitConjunction oc_a, oc_b;
    const bool found_a = screen_orbits(acme.state, orbx.state,
                                       acme.covariance, orbx.covariance,
                                       hbr, ARC, &oc_a);
    const bool found_b = screen_orbits(orbx.state, acme.state,
                                       orbx.covariance, acme.covariance,
                                       hbr, ARC, &oc_b);
    if (!found_a || !found_b) {
        std::printf("No conjunction found (unexpected).\n");
        return 1;
    }

    stamp(-oc_a.conj.t_ca, "both on-board searches sweep the 1024 s arc (J2):");
    std::printf("   ACME-1   TCA in %.3f s   miss %6.1f m   Pc = %.3e\n",
                oc_a.conj.t_ca, oc_a.conj.miss_distance, oc_a.conj.pc);
    std::printf("   ORBX-9   TCA in %.3f s   miss %6.1f m   Pc = %.3e\n",
                oc_b.conj.t_ca, oc_b.conj.miss_distance, oc_b.conj.pc);
    const bool pc_bits_equal =
        (bits_of(oc_a.conj.pc) == bits_of(oc_b.conj.pc)) &&
        (bits_of(oc_a.conj.t_ca) == bits_of(oc_b.conj.t_ca));
    std::printf("   Pc bits  ACME 0x%016llX | ORBX 0x%016llX  %s\n\n",
                static_cast<unsigned long long>(bits_of(oc_a.conj.pc)),
                static_cast<unsigned long long>(bits_of(oc_b.conj.pc)),
                pc_bits_equal ? "IDENTICAL [ok]" : "MISMATCH [BUG]");

    if (!actionable(oc_a.conj)) {
        std::printf("Not actionable. Nothing to do.\n");
        return 1;
    }

    // --- Layer 1: rule geometry is evaluated at the encounter ---
    const uint64_t tca_epoch_ms =
        T0_MS + static_cast<uint64_t>(oc_a.conj.t_ca * 1000.0);
    CraftDescriptor acme_tca = acme;
    acme_tca.state = oc_a.a_tca;
    CraftDescriptor orbx_tca = orbx;
    orbx_tca.state = oc_a.b_tca;

    const GiveWayVerdict v_acme =
        evaluate(acme_tca, orbx_tca, oc_a.conj, tca_epoch_ms);
    const GiveWayVerdict v_orbx =
        evaluate(orbx_tca, acme_tca, oc_b.conj, tca_epoch_ms);

    stamp(-oc_a.conj.t_ca, "each runs the Rules of the Orbital Road:");
    std::printf("   ACME-1   verdict: I %s   (%s)\n",
                v_acme.self_role == Role::GIVE_WAY ? "GIVE WAY" : "STAND ON",
                rule_str(v_acme.deciding_rule));
    std::printf("   ORBX-9   verdict: I %s   (%s)\n",
                v_orbx.self_role == Role::GIVE_WAY ? "GIVE WAY" : "STAND ON",
                rule_str(v_orbx.deciding_rule));
    const bool agree =
        id_equal(v_acme.give_way_id, v_orbx.give_way_id) &&
        (v_acme.conjunction_id == v_orbx.conjunction_id) &&
        (v_acme.deciding_rule == v_orbx.deciding_rule);
    std::printf("   conj id  0x%016llX on both craft\n",
                static_cast<unsigned long long>(v_acme.conjunction_id));
    std::printf("   independent agreement: %.12s gives way  %s\n\n",
                v_acme.give_way_id.c, agree ? "[ok] no message needed"
                                            : "[BUG] disagreement");

    stamp(-oc_a.conj.t_ca + 10.0, "optional handshake over shared relay:");
    std::printf("   ACME-1 -> ORBX-9   INTENT  \"conj 0x%08llX: I give way\"\n",
                static_cast<unsigned long long>(v_acme.conjunction_id >> 32));
    std::printf("   ORBX-9 -> ACME-1   ACK     \"agreed, I stand on\"\n");
    std::printf("   (comms CONFIRM the verdict; they never decide it)\n\n");

    // --- Layer 6: the give-way craft plans its minimum-dv burn ---
    const bool acme_gives = (v_acme.self_role == Role::GIVE_WAY);
    const CraftDescriptor& gw = acme_gives ? acme : orbx;
    const OrbitConjunction& goc = acme_gives ? oc_a : oc_b;
    const bool gw_is_lo =
        (id_compare(gw.object_id,
                    (acme_gives ? orbx : acme).object_id) <= 0);

    const GiveWayPlan plan =
        plan_give_way(gw.state, goc.a_tca, goc.conj, gw_is_lo);
    const double naive_dv =
        (canon::MANEUVER_MARGIN * canon::R_SAFE_M - goc.conj.miss_distance) /
        goc.conj.t_ca;

    stamp(-oc_a.conj.t_ca + 20.0, "give-way craft plans min-dv burn (Hill/CW):");
    std::printf("   dv = %.3f m/s   hill [radial %+.3f, along %+.3f, cross %+.3f]\n",
                plan.dv_mag, plan.dv_hill.x, plan.dv_hill.y, plan.dv_hill.z);
    std::printf("   naive linear impulse would cost %.3f m/s -> CW saves %.0f%%\n\n",
                naive_dv, 100.0 * (1.0 - plan.dv_mag / naive_dv));

    stamp(-oc_a.conj.t_ca + 30.0,
          "burn executed. ORBX-9 holds course - predictability");
    std::printf("           is the stand-on craft's entire job.\n\n");

    // Timeline to TCA: true inter-craft range from the canonical propagator.
    for (int k = 3; k >= 1; --k) {
        const double t = oc_a.conj.t_ca * (1.0 - 0.25 * static_cast<double>(k));
        const CraftState sa = propagate(canonicalize(acme.state), t);
        const CraftState sb = propagate(canonicalize(orbx.state), t);
        char line[96];
        std::snprintf(line, sizeof(line), "range %8.1f km, closing",
                      (sb.position - sa.position).norm() / 1000.0);
        stamp(t - oc_a.conj.t_ca, line);
    }

    const double miss_final = goc.conj.miss_distance + plan.displacement_m;
    char tca_line[96];
    std::snprintf(tca_line, sizeof(tca_line),
                  "TCA - closest approach %.1f m >= R_SAFE %.0f m  %s",
                  miss_final, canon::R_SAFE_M,
                  (miss_final >= canon::R_SAFE_M) ? "[ok]" : "[BUG]");
    stamp(0.0, tca_line);
    std::printf("\n");

    // --- Layer 3: return to slot, mirror + phase ---
    const double n_gw = mean_motion(gw.state);
    const MirrorReturnPlan ret =
        plan_mirror_return(plan.dv_hill, n_gw, canon::RETURN_PHASING_ORBITS);
    const double t_mirror = -oc_a.conj.t_ca + ret.wait_s;
    const HillState kick{Vec3{}, plan.dv_hill};
    const HillState off = cw_propagate(kick, n_gw, ret.wait_s);

    stamp(t_mirror, "one period after the burn: mirror + phase return");
    std::printf("   offset from slot: %.2f km, purely along-track (CW exact)\n",
                off.r.norm() / 1000.0);
    std::printf("   burn 1 (mirror+phase): %.3f m/s - orbit energy restored\n",
                ret.mirror_dv_hill.norm());
    std::printf("   phasing home over %d orbits (%.0f s)\n",
                canon::RETURN_PHASING_ORBITS, ret.transfer_s);

    HillState after1{off.r, off.v + ret.mirror_dv_hill};
    HillState arrive = cw_propagate(after1, n_gw, ret.transfer_s);
    arrive.v = arrive.v + ret.final_dv_hill;
    char back_line[96];
    std::snprintf(back_line, sizeof(back_line),
                  "burn 2 (%.3f m/s): offset %.3f m, drift %.6f m/s - in slot",
                  ret.final_dv_hill.norm(), arrive.r.norm(), arrive.v.norm());
    stamp(t_mirror + ret.transfer_s, back_line);
    std::printf("   fuel: give-way %.3f + return %.3f = %.3f m/s total\n",
                plan.dv_mag, ret.total_dv, plan.dv_mag + ret.total_dv);
    std::printf("   (analytically reversible: campaign ~ dv*(2 + 2/N))\n\n");

    const bool returned = (arrive.r.norm() < 1.0) && (arrive.v.norm() < 0.01);

    // ------------------------------------------------------------------
    // Act II — a derelict joins: three craft, one deterministic pass.
    // ------------------------------------------------------------------
    std::printf("-------------------------------------------------------------\n");
    std::printf(" Act II - a derelict joins the window (3-body, one pass)\n\n");

    // HULK: dead upper stage that crosses ACME-1 ~7 min out (before the
    // ORBX-9 encounter), 40 m above, on a 35 deg-tilted track. Built at its
    // crossing instant, then back-propagated to the CDM epoch.
    const double t_hulk = 420.0;                              // s, within arc
    CraftDescriptor hulk;
    hulk.object_id = make_object_id("1999-025DEB");
    hulk.tier = Tier::DEORBITING;
    hulk.can_maneuver = false;
    hulk.trust = TrustLevel::ASSERTED;     // nobody can verify a corpse
    {
        // ACME-1's state at the intended crossing instant.
        const CraftState a_cross = propagate(canonicalize(acme.state), t_hulk);
        const Vec3 rhat = a_cross.position.normalized();
        const Vec3 vhat = a_cross.velocity.normalized();
        const Vec3 chat = rhat.cross(vhat).normalized();      // cross-track
        const double r_h = a_cross.position.norm() + 40.0;
        const double v_h = std::sqrt(canon::MU_EARTH_M3S2 / r_h);
        // 40 m above ACME, velocity tilted 35 deg out of ACME's plane -> a
        // genuine crossing at t_hulk.
        const double tilt = 35.0 * canon::PI / 180.0;
        CraftState hulk_cross;
        hulk_cross.position = rhat * r_h;
        hulk_cross.velocity =
            (vhat * std::cos(tilt) + chat * std::sin(tilt)) * v_h;
        // Back-propagate to the shared CDM epoch.
        hulk.state = propagate(hulk_cross, -t_hulk);
    }

    OrbitConjunction oc_h;
    const bool found_h = screen_orbits(acme.state, hulk.state,
                                       acme.covariance, hulk.covariance,
                                       2.0 * canon::DEFAULT_HBR_M, ARC,
                                       &oc_h);
    if (!found_h) {
        std::printf("Derelict conjunction not found (unexpected).\n");
        return 1;
    }
    std::printf("   HULK   [1999-025DEB]  derelict stage, capability claim\n");
    std::printf("          UNVERIFIED; TCA in %.1f s, miss %.1f m from ACME-1\n\n",
                oc_h.conj.t_ca, oc_h.conj.miss_distance);

    const CraftDescriptor fleet[3] = {acme, orbx, hulk};
    WindowEntry win[2];
    win[0] = {0u, 2u, oc_h.conj,
              T0_MS + static_cast<uint64_t>(oc_h.conj.t_ca * 1000.0),
              oc_h.a_tca, oc_h.b_tca};
    win[1] = {0u, 1u, oc_a.conj, tca_epoch_ms, oc_a.a_tca, oc_a.b_tca};

    WindowVerdict wv[2];
    const uint32_t n_resolved = resolve_window(fleet, 3u, win, 2u, wv);

    const char* names[3] = {"ACME-1", "ORBX-9", "HULK  "};
    std::printf("   window: 3 craft, 2 conjunctions, canonical order:\n");
    for (uint32_t i = 0; i < n_resolved; ++i) {
        std::printf("   #%u  conj 0x%08llX  %s GIVES WAY to %s  (%s)%s\n",
                    i + 1u,
                    static_cast<unsigned long long>(wv[i].conjunction_id >> 32),
                    names[wv[i].give_way_index], names[wv[i].stand_on_index],
                    rule_str(wv[i].deciding_rule),
                    wv[i].escalated ? "  [ESCALATED]" : "");
    }
    std::printf("\n   The derelict's \"cannot maneuver\" is UNVERIFIED - the\n");
    std::printf("   conservative default treats it as debris and ACME-1 avoids\n");
    std::printf("   it anyway: a lie can shift fuel costs, never safety.\n");
    std::printf("   ACME-1, already burning, absorbs the ORBX-9 pair (cascade):\n");
    std::printf("   one combined maneuver; every craft in the window computed\n");
    std::printf("   this same assignment without exchanging a byte.\n\n");

    const bool act2_ok = (n_resolved == 2u) &&
                         (wv[0].give_way_index == 0u) &&
                         (wv[0].deciding_rule == DecidingRule::CAPABILITY) &&
                         (wv[1].give_way_index == 0u) &&
                         (wv[1].deciding_rule == DecidingRule::CASCADE);

    const bool ok = pc_bits_equal && agree && plan.required &&
                    (miss_final >= canon::R_SAFE_M) && returned && act2_ok;
    std::printf("=============================================================\n");
    std::printf(" %s\n", ok ? "DEMO PASSED - found, agreed, avoided, returned."
                            : "DEMO FAILED");
    std::printf("=============================================================\n");
    return ok ? 0 : 1;
}
