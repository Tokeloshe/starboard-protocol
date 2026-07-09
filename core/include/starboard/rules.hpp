// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — Layer 1: the Rules of the Orbital Road (SPEC §5).
//
// THE CANONICAL PAIR TRANSFORM (SPEC §5.2). evaluate() first orders the pair
// by canonical object id, then computes the verdict ONCE on the ordered
// (lo, hi) pair, then maps the result back to the caller's perspective.
// Agreement between the two craft (Theorem 1) is therefore true BY
// CONSTRUCTION: both run the identical function on the identical ordered
// arguments. There is no perspective-dependent arithmetic to get wrong, and
// an auditor can verify the property by inspection of this file alone.
//
// The rule chain is total: Rules 1-4 either decide or explicitly fall
// through, and Rule 5 (the fair coin) decides every remaining case from the
// conjunction id — deterministic, yet 50/50 over repeated encounters, so no
// craft is systematically the one that burns fuel (SPEC §5.7).
// ============================================================================

#include "covariance.hpp"
#include "id.hpp"
#include "trust.hpp"

namespace starboard {

// Everything the ruleset may consult about a craft. All fields are shared
// knowledge (catalog/registry-backed); `trust` is the local verification
// outcome for the counterpart's assertion (VERIFIED for self). For an
// actionable conjunction, `state` MUST be the canonical state AT TCA
// (screening.hpp returns it) — rule geometry describes the encounter.
struct CraftDescriptor {
    ObjectId   object_id;
    Tier       tier         = Tier::OPERATIONAL;
    bool       can_maneuver = true;
    TrustLevel trust        = TrustLevel::VERIFIED;
    double     hard_body_radius_m = canon::DEFAULT_HBR_M;
    CraftState state;                       // canonical ECI state
    Sym33      covariance = Sym33::diagonal(canon::DEFAULT_VAR_M2);
};

enum class Role : uint8_t { GIVE_WAY, STAND_ON };

// Which rule decided — for logging, the handshake cross-check, and the
// conformance suite.
enum class DecidingRule : uint8_t {
    CAPABILITY = 1,  // Rule 1: the able craft gives way to the unable one
    TIER       = 2,  // Rule 2: lower registry tier gives way
    OVERTAKING = 3,  // Rule 3: the overtaking craft gives way
    STARBOARD  = 4,  // Rule 4: the craft seeing the other to starboard gives way
    FAIR_COIN  = 5,  // Rule 5: conjunction-id coin (deterministic, fuel-fair)
    CASCADE    = 6   // Layer 7 multi-body lock carried a role forward
};

struct GiveWayVerdict {
    Role         self_role;
    DecidingRule deciding_rule;
    ObjectId     give_way_id;     // the craft that MUST maneuver
    uint64_t     conjunction_id;  // shared id (both derive it independently)
    bool         advisory;        // true if NEITHER craft can maneuver:
                                  // verdict is for alerting only
};

// ---------------------------------------------------------------------------
// Conjunction id (SPEC §9.3 / PROTOCOL Appendix B). FNV-1a-64 over the
// canonical bytes: lo id (12) || hi id (12) || TCA bucket (8, LE) ||
// RULESET_ID (4, LE). Folding the ruleset version into the id means a fork
// that changes the rules derives DIFFERENT conjunction ids — it is
// off-network at the identifier level, before any verdict is compared.
// ---------------------------------------------------------------------------
inline uint64_t conjunction_id(const ObjectId& a, const ObjectId& b,
                               uint64_t tca_epoch_ms) {
    const bool a_lo = (id_compare(a, b) <= 0);
    const ObjectId& lo = a_lo ? a : b;
    const ObjectId& hi = a_lo ? b : a;

    uint64_t h = 14695981039346656037ull;          // FNV-1a offset basis
    constexpr uint64_t prime = 1099511628211ull;   // FNV-1a prime
    const auto mix = [&h](uint8_t byte) {
        h ^= static_cast<uint64_t>(byte);
        h *= prime;
    };
    for (int i = 0; i < 12; ++i) { mix(static_cast<uint8_t>(lo.c[i])); }
    for (int i = 0; i < 12; ++i) { mix(static_cast<uint8_t>(hi.c[i])); }
    const uint64_t bucket = tca_epoch_ms / canon::TCA_BUCKET_MS;
    for (int i = 0; i < 8; ++i) { mix(static_cast<uint8_t>(bucket >> (8 * i))); }
    for (int i = 0; i < 4; ++i) {
        mix(static_cast<uint8_t>(canon::RULESET_ID >> (8 * i)));
    }
    return h;
}

namespace detail {

// Rule geometry is evaluated at the canonical states AT TCA (SPEC §5.1) and
// both predicates below are ASYMPTOTE forms: they depend on the (nearly
// constant) velocities through the encounter, not on where along the
// approach the evaluation happens — so the answer is epoch-robust as well as
// swap-deterministic.

// Rule 3 predicate (SPEC §5.5): does x overtake y? Both conditions carry a
// dead-band so quantization noise cannot flip the answer:
//   (a) x is strictly faster than y (by more than EPS_SPEED);
//   (b) x's motion RELATIVE to y is aligned with y's own heading within the
//       overtake cone (67.5 deg) — i.e. x runs up y's track from astern.
//       (In the far past x's bearing from y is -(vx-vy): condition (b) is
//       exactly "x approaches from y's stern sector".)
inline bool overtakes(const CraftDescriptor& x, const CraftDescriptor& y) {
    const double sx = x.state.velocity.norm();
    const double sy = y.state.velocity.norm();
    if ((sx - sy) <= canon::EPS_SPEED_MPS) { return false; }
    if (sy <= canon::EPS_SPEED_MPS) { return false; }
    const Vec3 dv = x.state.velocity - y.state.velocity;
    const double dvn = dv.norm();
    if (dvn <= canon::EPS_SPEED_MPS) { return false; }
    return dv.dot(y.state.velocity) > (canon::OVERTAKE_COS * dvn) * sy;
}

// Rule 4 predicate (SPEC §5.6): does x see y approaching on its starboard
// side? Body frame: forward = velocity, down = -zenith; starboard =
// down x forward = velocity_hat x position_hat (facing east with zenith up,
// starboard is south — the handedness is normative). The APPROACH BEARING of
// y as seen from x is -(vy - vx): y's relative position in the past is
// miss - dv*dt, dominated by -dv. x "sees y to starboard" iff that bearing
// has a starboard component above the EPS_BEARING dead-band — the exact
// orbital transplant of COLREGS "has the other on her own starboard side".
// Co-drifting pairs (no approach asymptote) fall back to the instantaneous
// bearing with the EPS_LATERAL dead-band.
inline bool sees_on_starboard(const CraftDescriptor& x,
                              const CraftDescriptor& y) {
    const Vec3 starboard =
        x.state.velocity.normalized().cross(x.state.position.normalized());
    const Vec3 dv = y.state.velocity - x.state.velocity;
    const double dvn = dv.norm();
    if (dvn <= canon::EPS_SPEED_MPS) {
        const Vec3 to_y = y.state.position - x.state.position;
        return to_y.dot(starboard) > canon::EPS_LATERAL_M;
    }
    return -(dv.dot(starboard)) > canon::EPS_BEARING * dvn;
}

} // namespace detail

// ---------------------------------------------------------------------------
// evaluate — the Rules of the Orbital Road (SPEC §5.3).
//
// `self` is the craft running the computation; `other` the counterpart;
// `tca_epoch_ms` the shared TCA epoch (bit-identical on both craft because it
// derives from quantized shared inputs). Theorem 1 (SPEC §5.8): for any pair
// with canonical shared inputs, evaluate(A,B) and evaluate(B,A) return the
// same give_way_id, deciding_rule and conjunction_id.
// ---------------------------------------------------------------------------
inline GiveWayVerdict evaluate(const CraftDescriptor& self,
                               const CraftDescriptor& other,
                               const Conjunction& conj,
                               uint64_t tca_epoch_ms) {
    (void)conj;  // the verdict depends only on shared states + registry facts;
                 // conj gates WHETHER evaluate runs (see actionable()).

    // Canonical Pair Transform: order by id, decide once on (lo, hi).
    const bool self_is_lo = (id_compare(self.object_id, other.object_id) <= 0);
    const CraftDescriptor& lo = self_is_lo ? self : other;
    const CraftDescriptor& hi = self_is_lo ? other : self;

    const uint64_t cid =
        conjunction_id(lo.object_id, hi.object_id, tca_epoch_ms);

    bool lo_gives_way;
    DecidingRule rule;

    const bool cm_lo = effective_can_maneuver(lo.trust, lo.can_maneuver);
    const bool cm_hi = effective_can_maneuver(hi.trust, hi.can_maneuver);
    const Tier tier_lo = effective_tier(lo.trust, lo.tier);
    const Tier tier_hi = effective_tier(hi.trust, hi.tier);

    if (cm_lo != cm_hi) {
        // Rule 1 — capability. The able craft gives way.
        lo_gives_way = cm_lo;
        rule = DecidingRule::CAPABILITY;
    } else if (tier_lo != tier_hi) {
        // Rule 2 — registry tier. Lower tier gives way.
        lo_gives_way = (static_cast<uint8_t>(tier_lo) <
                        static_cast<uint8_t>(tier_hi));
        rule = DecidingRule::TIER;
    } else {
        const bool lo_overtakes = detail::overtakes(lo, hi);
        const bool hi_overtakes = detail::overtakes(hi, lo);
        if (lo_overtakes != hi_overtakes) {
            // Rule 3 — the overtaking craft gives way.
            lo_gives_way = lo_overtakes;
            rule = DecidingRule::OVERTAKING;
        } else {
            const bool lo_sees = detail::sees_on_starboard(lo, hi);
            const bool hi_sees = detail::sees_on_starboard(hi, lo);
            if (lo_sees != hi_sees) {
                // Rule 4 — starboard. The craft seeing the other to
                // starboard gives way (crossing AND head-on geometries).
                lo_gives_way = lo_sees;
                rule = DecidingRule::STARBOARD;
            } else {
                // Rule 5 — the fair coin. LSB of the conjunction id:
                // 0 -> lo gives way, 1 -> hi. Total (always defined),
                // deterministic (both derive cid), and fuel-fair over
                // repeated encounters (the TCA bucket varies per event).
                lo_gives_way = ((cid & 1ull) == 0ull);
                rule = DecidingRule::FAIR_COIN;
            }
        }
    }

    GiveWayVerdict v;
    v.deciding_rule = rule;
    v.conjunction_id = cid;
    v.give_way_id = lo_gives_way ? lo.object_id : hi.object_id;
    v.advisory = (!cm_lo) && (!cm_hi);
    v.self_role = id_equal(v.give_way_id, self.object_id) ? Role::GIVE_WAY
                                                          : Role::STAND_ON;
    return v;
}

} // namespace starboard
