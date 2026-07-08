// ============================================================================
// Starboard demo — two rival-operator satellites de-conflict a crossing.
//
// Neither satellite talks to the other. Each independently screens the
// conjunction and runs the Rules of the Orbital Road. The point of the demo is
// that BOTH arrive at the identical verdict for who gives way — then the
// give-way craft computes a maneuver that clears the miss distance.
//
//   Build:  see CMakeLists.txt   (target: starboard_crossing_demo)
// ============================================================================

#include "starboard/rules.hpp"
#include <cstdio>

using namespace starboard;

static const char* role_str(Role r) { return r == Role::GIVE_WAY ? "GIVE WAY" : "STAND ON"; }
static const char* rule_str(DecidingRule r) {
    switch (r) {
        case DecidingRule::CAPABILITY: return "Rule 1 (capability)";
        case DecidingRule::TIER:       return "Rule 2 (tier)";
        case DecidingRule::OVERTAKING: return "Rule 3 (overtaking)";
        case DecidingRule::STARBOARD:  return "Rule 4 (starboard crossing)";
        case DecidingRule::TIEBREAK:   return "Rule 5 (tie-break)";
    }
    return "?";
}

int main() {
    // Two craft from different operators on crossing tracks in LEO (~7.6 km/s).
    // ACME-1 flies +X; ORBX-9 flies +Y and will cross ACME-1's path ahead of it.
    CraftDescriptor acme;
    acme.object_id = "2026-001A";           // ACME Space
    acme.tier = Tier::OPERATIONAL;
    acme.can_maneuver = true;
    acme.state.position = Vec3(0.0,     0.0, 6.93e6);
    acme.state.velocity = Vec3(7600.0,  0.0, 0.0);

    CraftDescriptor orbx;
    orbx.object_id = "2026-044B";           // OrbX (a competitor)
    orbx.tier = Tier::OPERATIONAL;
    orbx.can_maneuver = true;
    orbx.state.position = Vec3(38000.0, -38000.0, 6.93e6 + 200.0); // 5 s out, 200 m off-plane
    orbx.state.velocity = Vec3(0.0,      7600.0,  0.0);

    const double horizon_s = 3.0 * 5400.0; // ~3 orbital periods

    // --- Each craft screens the pair independently (Layer 0) ---
    Conjunction c_acme = screen(acme.state, orbx.state, horizon_s);
    Conjunction c_orbx = screen(orbx.state, acme.state, horizon_s);

    printf("=== Starboard crossing demo ===\n\n");
    printf("ACME-1 (%s) screens: detected=%d  TCA=%.1fs  miss=%.1f m\n",
           acme.object_id.c_str(), c_acme.detected, c_acme.t_ca, c_acme.miss_distance);
    printf("ORBX-9 (%s) screens: detected=%d  TCA=%.1fs  miss=%.1f m\n\n",
           orbx.object_id.c_str(), c_orbx.detected, c_orbx.t_ca, c_orbx.miss_distance);

    if (!c_acme.detected) { printf("No conjunction. Nothing to do.\n"); return 0; }

    // --- Each craft runs the Rules of the Orbital Road independently (Layer 1) ---
    GiveWayVerdict v_acme = evaluate(acme, orbx, c_acme);
    GiveWayVerdict v_orbx = evaluate(orbx, acme, c_orbx);

    printf("ACME-1 decides: I %s  (%s) -> give-way craft = %s\n",
           role_str(v_acme.self_role), rule_str(v_acme.deciding_rule), v_acme.give_way_id.c_str());
    printf("ORBX-9 decides: I %s  (%s) -> give-way craft = %s\n\n",
           role_str(v_orbx.self_role), rule_str(v_orbx.deciding_rule), v_orbx.give_way_id.c_str());

    // --- The whole point: both independently name the SAME give-way craft ---
    const bool agree = (v_acme.give_way_id == v_orbx.give_way_id);
    printf("Independent agreement on who gives way: %s\n", agree ? "YES ✓" : "NO ✗ (bug!)");

    // --- The give-way craft computes its maneuver (Layer 1 maneuver) ---
    const bool acme_gives_way = (v_acme.self_role == Role::GIVE_WAY);
    const CraftState& gw = acme_gives_way ? acme.state : orbx.state;
    const Conjunction& gc = acme_gives_way ? c_acme : c_orbx;
    Vec3 burn = give_way_burn(gw, gc);
    printf("\nGive-way craft %s burns Δv = (%.4f, %.4f, %.4f) m/s  |Δv| = %.4f m/s\n",
           v_acme.give_way_id.c_str(), burn.x, burn.y, burn.z, burn.norm());
    printf("(minimum nudge to reach R_safe = %.0f m, undone on return-to-slot)\n",
           limits::R_SAFE_M);

    return agree ? 0 : 1;
}
