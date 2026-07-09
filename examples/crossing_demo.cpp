// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.

// ============================================================================
// Starboard demo — the Rules of the Orbital Road, live.
//
// Act I : two rival-operator satellites, zero communication, independently
//         compute identical Pc bits and the identical give-way verdict; the
//         give-way craft flies a minimum-dv Hill-frame burn, clears the
//         conjunction, and flocks back to its slot with a two-impulse return.
// Act II: a third, uncooperative derelict joins the window; the multi-body
//         resolver orders and settles all pairs in one deterministic pass.
//
//   Build:  cmake -B build && cmake --build build
//   Run:    ./build/starboard_crossing_demo
// ============================================================================

#include "starboard/starboard.hpp"

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
    std::printf(" T%c%02d:%02d  %s\n", (t_rel_s < 0.0) ? '-' : '+',
                static_cast<int>(a) / 60, static_cast<int>(a) % 60, msg);
}

} // namespace

int main() {
    std::printf("=============================================================\n");
    std::printf(" STARBOARD - the Rules of the Orbital Road, live\n");
    std::printf("=============================================================\n\n");

    // ------------------------------------------------------------------
    // Act I — two rivals on crossing tracks, TCA 45 minutes out.
    // ------------------------------------------------------------------
    const double R_LEO = 6.93e6;    // m
    const double V_LEO = 7600.0;    // m/s
    const double LEAD  = 2700.0;    // s to TCA at detection
    const uint64_t TCA_EPOCH_MS = 1401000000000ull;  // shared GPS epoch

    // Crossing point ~(0,0,R): ACME flies +x, ORBX flies +y and will pass
    // 150 m above. States are the shared-catalog relative-motion convention.
    CraftDescriptor acme;
    acme.object_id = make_object_id("2026-001A");
    acme.tier = Tier::OPERATIONAL;
    acme.state.position = Vec3(0.0, 0.0, R_LEO);
    acme.state.velocity = Vec3(V_LEO, 0.0, 0.0);
    acme.covariance = Sym33::diagonal(9.0e4);        // sigma = 300 m
    acme.state = canonicalize(acme.state);

    CraftDescriptor orbx;
    orbx.object_id = make_object_id("2026-044B");
    orbx.tier = Tier::OPERATIONAL;
    orbx.state.position = Vec3(V_LEO * LEAD, -V_LEO * LEAD, R_LEO + 150.0);
    orbx.state.velocity = Vec3(0.0, V_LEO, 0.0);
    orbx.covariance = Sym33::diagonal(9.0e4);
    orbx.state = canonicalize(orbx.state);

    const double horizon = 3.0 * 5700.0;   // ~3 orbital periods

    std::printf(" Act I - two rivals, zero communication\n");
    std::printf("   ACME-1 [2026-001A]  ACME Space    tier OPERATIONAL\n");
    std::printf("   ORBX-9 [2026-044B]  OrbX (rival)  tier OPERATIONAL\n\n");

    // Each craft screens independently (Layer 0).
    const double hbr = acme.hard_body_radius_m + orbx.hard_body_radius_m;
    Conjunction c_acme = assess(acme.state, orbx.state, acme.covariance,
                                orbx.covariance, hbr, horizon);
    Conjunction c_orbx = assess(orbx.state, acme.state, orbx.covariance,
                                acme.covariance, hbr, horizon);

    stamp(-LEAD, "both on-board screens run, independently:");
    std::printf("   ACME-1   Pc = %.3e   miss %6.1f m   TCA in %.1f s\n",
                c_acme.pc, c_acme.miss_distance, c_acme.t_ca);
    std::printf("   ORBX-9   Pc = %.3e   miss %6.1f m   TCA in %.1f s\n",
                c_orbx.pc, c_orbx.miss_distance, c_orbx.t_ca);
    const bool pc_bits_equal = (bits_of(c_acme.pc) == bits_of(c_orbx.pc));
    std::printf("   Pc bits  ACME 0x%016llX | ORBX 0x%016llX  %s\n\n",
                static_cast<unsigned long long>(bits_of(c_acme.pc)),
                static_cast<unsigned long long>(bits_of(c_orbx.pc)),
                pc_bits_equal ? "IDENTICAL [ok]" : "MISMATCH [BUG]");

    if (!actionable(c_acme)) {
        std::printf("No actionable conjunction. Nothing to do.\n");
        return 1;
    }

    // Each craft runs the Rules of the Orbital Road (Layer 1).
    const GiveWayVerdict v_acme = evaluate(acme, orbx, c_acme, TCA_EPOCH_MS);
    const GiveWayVerdict v_orbx = evaluate(orbx, acme, c_orbx, TCA_EPOCH_MS);

    stamp(-LEAD, "each runs the Rules of the Orbital Road:");
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

    // Optional Layer-2 handshake, confirming what both already know.
    stamp(-LEAD + 10.0, "optional handshake over shared relay (Layer 2):");
    std::printf("   ACME-1 -> ORBX-9   INTENT  \"conj 0x%08llX: I give way\"\n",
                static_cast<unsigned long long>(v_acme.conjunction_id >> 32));
    std::printf("   ORBX-9 -> ACME-1   ACK     \"agreed, I stand on\"\n");
    std::printf("   (comms CONFIRM the verdict; they never decide it)\n\n");

    // The give-way craft plans its minimum-dv burn (Layer 6).
    const bool acme_gives = (v_acme.self_role == Role::GIVE_WAY);
    const CraftDescriptor& gw   = acme_gives ? acme : orbx;
    const Conjunction&     gc   = acme_gives ? c_acme : c_orbx;
    const bool gw_is_lo =
        (id_compare(gw.object_id,
                    (acme_gives ? orbx : acme).object_id) <= 0);

    const GiveWayPlan plan = plan_give_way(gw.state, gc, gw_is_lo);
    const double naive_dv =
        (canon::MANEUVER_MARGIN * canon::R_SAFE_M - gc.miss_distance) / gc.t_ca;

    stamp(-LEAD + 20.0, "give-way craft plans minimum-dv burn (Hill/CW):");
    std::printf("   dv = %.3f m/s   hill [radial %+.3f, along %+.3f, cross %+.3f]\n",
                plan.dv_mag, plan.dv_hill.x, plan.dv_hill.y, plan.dv_hill.z);
    std::printf("   naive linear impulse would cost %.3f m/s -> CW saves %.0f%%\n\n",
                naive_dv, 100.0 * (1.0 - plan.dv_mag / naive_dv));

    stamp(-LEAD + 30.0, "burn executed. ORBX-9 holds course - predictability");
    std::printf("          is the stand-on craft's entire job.\n\n");

    // Timeline to TCA: separation along the way (linear pair + CW deviation).
    const double n_gw = mean_motion(gw.state);
    const HillState kick{Vec3{}, plan.dv_hill};
    for (double t = LEAD / 3.0; t < LEAD - 1.0; t += LEAD / 3.0) {
        const Vec3 dr = (gc.miss_vector - gc.rel_velocity * (gc.t_ca - t)) -
                        hill_basis(gw.state).tmul(
                            cw_propagate(kick, n_gw, t).r);
        char line[96];
        std::snprintf(line, sizeof(line),
                      "range %8.1f km, closing", dr.norm() / 1000.0);
        stamp(-LEAD + t, line);
    }

    const double miss_final = gc.miss_distance + plan.displacement_m;
    char tca_line[96];
    std::snprintf(tca_line, sizeof(tca_line),
                  "TCA - closest approach %.1f m >= R_SAFE %.0f m  %s",
                  miss_final, canon::R_SAFE_M,
                  (miss_final >= canon::R_SAFE_M) ? "[ok]" : "[BUG]");
    stamp(0.0, tca_line);
    std::printf("\n");

    // Layer 3 — return to slot: mirror + phase (the flock settles back).
    const MirrorReturnPlan ret =
        plan_mirror_return(plan.dv_hill, n_gw, canon::RETURN_PHASING_ORBITS);
    const double t_mirror = -LEAD + ret.wait_s;    // rel. TCA
    const HillState off = cw_propagate(kick, n_gw, ret.wait_s);

    stamp(t_mirror, "one period after the burn: mirror + phase return");
    std::printf("   offset from slot: %.2f km, purely along-track (CW exact)\n",
                off.r.norm() / 1000.0);
    std::printf("   burn 1 (mirror+phase): %.3f m/s - orbit energy restored\n",
                ret.mirror_dv_hill.norm());
    std::printf("   phasing home over %d orbits (%.0f s)\n",
                canon::RETURN_PHASING_ORBITS, ret.transfer_s);

    // Verify the return analytically: propagate the corrected state.
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
    std::printf("   (the give-way burn is analytically reversible: the whole\n");
    std::printf("    avoidance cost ~ dv*(2 + 2/N) - Theorem 3)\n\n");

    const bool returned = (arrive.r.norm() < 1.0) && (arrive.v.norm() < 0.01);

    // ------------------------------------------------------------------
    // Act II — a derelict joins: three craft, one deterministic pass.
    // ------------------------------------------------------------------
    std::printf("-------------------------------------------------------------\n");
    std::printf(" Act II - a derelict joins the window (3-body, one pass)\n\n");

    CraftDescriptor hulk;
    hulk.object_id = make_object_id("1999-025DEB");
    hulk.tier = Tier::DEORBITING;
    hulk.can_maneuver = false;
    hulk.trust = TrustLevel::ASSERTED;     // nobody can verify a corpse
    hulk.state.position = Vec3(-V_LEO * 900.0, 60.0, R_LEO + 40.0);
    hulk.state.velocity = Vec3(V_LEO * 1.01, 0.0, 0.0);
    hulk.state = canonicalize(hulk.state);

    const CraftDescriptor fleet[3] = {acme, orbx, hulk};

    WindowEntry win[2];
    win[0] = {0u, 2u,
              assess(acme.state, hulk.state, acme.covariance, hulk.covariance,
                     hbr, horizon),
              TCA_EPOCH_MS - 1800000ull};   // derelict pass 30 min earlier
    win[1] = {0u, 1u, c_acme, TCA_EPOCH_MS};

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
    std::printf("   one combined maneuver, and every craft in the window\n");
    std::printf("   computed this same assignment without exchanging a byte.\n\n");

    const bool act2_ok = (n_resolved == 2u) &&
                         (wv[0].give_way_index == 0u) &&
                         (wv[0].deciding_rule == DecidingRule::CAPABILITY) &&
                         (wv[1].give_way_index == 0u) &&
                         (wv[1].deciding_rule == DecidingRule::CASCADE);

    const bool ok = pc_bits_equal && agree && plan.required &&
                    (miss_final >= canon::R_SAFE_M) && returned && act2_ok;
    std::printf("=============================================================\n");
    std::printf(" %s\n", ok ? "DEMO PASSED - independent agreement, safe miss,"
                              " slot restored." : "DEMO FAILED");
    std::printf("=============================================================\n");
    return ok ? 0 : 1;
}
