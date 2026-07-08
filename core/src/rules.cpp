// ============================================================================
// Starboard — Layer 1 reference implementation.
//
// The give-way logic is intentionally small and branch-for-branch auditable:
// determinism is the product, so the code must be trivially verifiable against
// the spec (docs/DESIGN.md §3).
// ============================================================================

#include "starboard/rules.hpp"

namespace starboard {

namespace {

// Package a verdict from the point of view of `self`, given which id gives way.
GiveWayVerdict make_verdict(const CraftDescriptor& self,
                            const std::string& give_way_id,
                            DecidingRule rule) {
    GiveWayVerdict v;
    v.deciding_rule = rule;
    v.give_way_id = give_way_id;
    v.self_role = (self.object_id == give_way_id) ? Role::GIVE_WAY : Role::STAND_ON;
    return v;
}

// Is `mover` overtaking `target`? Approach from behind: the relative approach
// direction is closely anti-parallel to the target's velocity (Rule 3).
bool is_overtaking(const CraftDescriptor& mover, const CraftDescriptor& target) {
    const Vec3 v_tgt = target.state.velocity.normalized();
    // Direction from mover to target, i.e. the line mover is closing along.
    const Vec3 approach = (target.state.position - mover.state.position).normalized();
    // Overtaking if mover is behind target and moving up its tail:
    // approach direction aligns with the target's velocity within the cone.
    return approach.dot(v_tgt) > limits::OVERTAKE_COS;
}

// Starboard handedness (Rule 4). Body frame: forward = velocity, down = nadir
// (−position, i.e. toward Earth center), starboard = forward × down. The craft
// that has the OTHER on its starboard side gives way.
// Returns true if `other` lies on `self`'s starboard side.
bool other_is_on_starboard(const CraftDescriptor& self, const CraftDescriptor& other) {
    const Vec3 forward = self.state.velocity.normalized();
    const Vec3 down = (self.state.position * -1.0).normalized(); // toward Earth center
    const Vec3 starboard = forward.cross(down);
    const Vec3 to_other = other.state.position - self.state.position;
    return to_other.dot(starboard) > 0.0;
}

} // namespace

GiveWayVerdict evaluate(const CraftDescriptor& self,
                        const CraftDescriptor& other,
                        const Conjunction& /*conj*/) {

    // Rule 1 — Capability. A craft that cannot maneuver stands on; the able
    // craft gives way. If neither can maneuver, fall through (nothing to do but
    // the tie-break still names one, for logging/alerting).
    if (self.can_maneuver != other.can_maneuver) {
        const std::string give_way = self.can_maneuver ? self.object_id : other.object_id;
        return make_verdict(self, give_way, DecidingRule::CAPABILITY);
    }

    // Rule 2 — Published tier. Lower tier gives way.
    if (self.tier != other.tier) {
        const std::string give_way =
            (static_cast<uint8_t>(self.tier) < static_cast<uint8_t>(other.tier))
                ? self.object_id : other.object_id;
        return make_verdict(self, give_way, DecidingRule::TIER);
    }

    // Rule 3 — Overtaking. The overtaking craft gives way. Evaluated
    // symmetrically: check whether either craft is overtaking the other.
    const bool self_overtaking = is_overtaking(self, other);
    const bool other_overtaking = is_overtaking(other, self);
    if (self_overtaking != other_overtaking) {
        const std::string give_way = self_overtaking ? self.object_id : other.object_id;
        return make_verdict(self, give_way, DecidingRule::OVERTAKING);
    }

    // Rule 4 — Starboard crossing. The craft with the other on its starboard
    // side gives way. In a genuine crossing exactly one of these is true; if
    // the geometry is ambiguous (both/neither), fall through to the tie-break.
    const bool other_on_self_starboard = other_is_on_starboard(self, other);
    const bool self_on_other_starboard = other_is_on_starboard(other, self);
    if (other_on_self_starboard != self_on_other_starboard) {
        const std::string give_way =
            other_on_self_starboard ? self.object_id : other.object_id;
        return make_verdict(self, give_way, DecidingRule::STARBOARD);
    }

    // Rule 5 — Deterministic tie-break. Higher canonical id gives way.
    const std::string give_way =
        (self.object_id > other.object_id) ? self.object_id : other.object_id;
    return make_verdict(self, give_way, DecidingRule::TIEBREAK);
}

Vec3 give_way_burn(const CraftState& self, const Conjunction& conj, double r_safe) {
    if (conj.miss_distance >= r_safe) return Vec3{}; // already clear

    // We need to push the miss vector out to r_safe. Cheapest direction is
    // perpendicular to relative motion, along the current miss vector (radial
    // separation), which a later return-to-slot burn can symmetrically undo.
    Vec3 sep_dir = conj.miss_vector.normalized();
    if (sep_dir.norm() < 1e-6) {
        // Degenerate head-on case: miss vector ~0, so there is no natural
        // separation direction. Push perpendicular to our own velocity (a
        // cross-track nudge), which is always well-defined and cheap to undo.
        const Vec3 fwd = self.velocity.normalized();
        Vec3 up(0.0, 0.0, 1.0);
        if (std::fabs(fwd.dot(up)) > 0.9) up = Vec3(1.0, 0.0, 0.0); // avoid degeneracy
        sep_dir = fwd.cross(up).normalized();
    }
    const double shortfall = r_safe - conj.miss_distance; // meters of extra separation needed

    // Apply the separation over the time available: Δv ≈ Δmiss / t_ca.
    // Guard against a near-zero TCA to avoid an unbounded burn.
    const double t = (conj.t_ca > 1.0) ? conj.t_ca : 1.0;
    return sep_dir * (shortfall / t);
}

} // namespace starboard
