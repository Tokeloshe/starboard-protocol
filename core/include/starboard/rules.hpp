#pragma once

// ============================================================================
// Starboard — Layer 1: the Rules of the Orbital Road
//
// The deterministic give-way ruleset. Given a declared conjunction and the two
// craft's public descriptors, both satellites independently compute the SAME
// verdict for who gives way — with no communication. See docs/DESIGN.md §3.
// ============================================================================

#include "geometry.hpp"
#include <cstdint>
#include <string>

namespace starboard {

// Static, publicly-advertised criticality tier (docs/DESIGN.md Rule 2).
// Higher value = higher priority = stands on.
enum class Tier : uint8_t {
    DEORBITING  = 0,
    OPERATIONAL = 1,
    CRITICAL    = 2,   // critical infrastructure / high-value cluster
    CREWED      = 3
};

// Everything about a craft that the ruleset needs. All fields are public and
// symmetric-knowledge: both craft in a pair know both descriptors.
struct CraftDescriptor {
    std::string object_id;   // canonical COSPAR/NORAD id (fixed-width, comparable)
    Tier        tier = Tier::OPERATIONAL;
    bool        can_maneuver = true;  // false = "not under command" (Rule 1)
    CraftState  state;
};

enum class Role : uint8_t { GIVE_WAY, STAND_ON };

// Which rule decided the outcome — for logging / the conformance suite.
enum class DecidingRule : uint8_t {
    CAPABILITY = 1,  // Rule 1: a craft that cannot maneuver stands on
    TIER       = 2,  // Rule 2: higher published tier stands on
    OVERTAKING = 3,  // Rule 3: the overtaking craft gives way
    STARBOARD  = 4,  // Rule 4: craft with the other on its starboard side gives way
    TIEBREAK   = 5   // Rule 5: deterministic id tie-break
};

struct GiveWayVerdict {
    Role         self_role;      // role of the craft that ran this evaluation
    DecidingRule deciding_rule;
    std::string  give_way_id;    // canonical id of the craft that must maneuver
};

// Evaluate the Rules of the Orbital Road for a declared conjunction.
//
// `self` is the craft running the computation; `other` is the neighbour.
// The result is symmetric: self.evaluate(other) and other.evaluate(self) name
// the SAME give_way_id and the SAME deciding_rule.
GiveWayVerdict evaluate(const CraftDescriptor& self,
                        const CraftDescriptor& other,
                        const Conjunction& conj);

// Minimum-Δv give-way maneuver: the burn (m/s, as a velocity delta) that raises
// the miss distance to R_safe at TCA. Returns a small radial/in-track nudge that
// is cheap to undo during return-to-slot (Layer 3). Zero vector if already safe.
Vec3 give_way_burn(const CraftState& self, const Conjunction& conj,
                   double r_safe = limits::R_SAFE_M);

} // namespace starboard
