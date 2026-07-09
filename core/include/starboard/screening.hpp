// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — canonical conjunction search (SPEC §4.2a).
//
// Operates on the shared reference-epoch bundle (SPEC §3.3): both craft's
// canonical states at a common epoch near TCA. It propagates both along the
// canonical J2 propagator (propagator.hpp) over the BOUNDED arc [0, arc],
// arc <= SCREEN_ARC_MAX_S, finds range-rate sign changes on the 32 s grid,
// refines each candidate TCA with a fixed 48-step bisection (the sign of
// g(t) = dr.dv is a shared, bit-deterministic quantity), then evaluates the
// encounter kernel — miss geometry and Starboard-Pc — at the refined TCA.
//
// The bounded arc is the point: over <= 1024 s the propagator matches truth
// to ~1 m, so the shared computation is not merely deterministic, it is
// deterministic AND physically valid to well inside the covariance. Longer
// lead times are handled operationally by re-screening against a fresh
// reference-epoch bundle as TCA approaches (SPEC §4.2a, rolling CDMs), not by
// extrapolating an unphysically long arc.
//
// The altitude-band prefilter (periapsis/apoapsis rings farther apart than
// R_SCREEN can never conjunct) rejects most catalog pairs with a few square
// roots and no propagation. Radial bounds come from vis-viva + angular
// momentum — algebraic, no atan2.
// ============================================================================

#include "covariance.hpp"
#include "propagator.hpp"

namespace starboard {

// A found conjunction plus the canonical states at TCA — Layer 1 evaluates
// rule geometry AT the encounter (SPEC §5.1), so the screener returns it.
// t_ca is relative to the shared reference epoch.
struct OrbitConjunction {
    Conjunction conj;
    CraftState  a_tca;
    CraftState  b_tca;
};

namespace detail {

// Radial bounds [periapsis, apoapsis] from a state via vis-viva — algebraic,
// no atan2. Non-elliptic (energy >= 0) is reported unbounded so it is never
// prefiltered out.
inline void radial_bounds(const CraftState& s, double* r_lo, double* r_hi) {
    const double rn = s.position.norm();
    const double v2 = s.velocity.dot(s.velocity);
    const double energy = 0.5 * v2 - canon::MU_EARTH_M3S2 / rn;
    if (energy >= 0.0) {
        *r_lo = 0.0;
        *r_hi = 1.0e18;
        return;
    }
    const double a = -canon::MU_EARTH_M3S2 / (2.0 * energy);
    const double h = s.position.cross(s.velocity).norm();
    double e2 = 1.0 + (2.0 * energy * h * h) /
                          (canon::MU_EARTH_M3S2 * canon::MU_EARTH_M3S2);
    if (e2 < 0.0) { e2 = 0.0; }
    const double e = std::sqrt(e2);
    *r_lo = a * (1.0 - e);
    *r_hi = a * (1.0 + e);
}

} // namespace detail

// Canonical pairwise search over [0, arc]. `a0`,`b0` are the shared states at
// the reference epoch; `arc_s` is clamped to SCREEN_ARC_MAX_S. Returns true
// and fills `out` with the EARLIEST detected conjunction; false if clean.
inline bool screen_orbits(const CraftState& a0_in, const CraftState& b0_in,
                          const Sym33& cov_a, const Sym33& cov_b,
                          double r_hb, double arc_s,
                          OrbitConjunction* out) {
    const CraftState a0 = canonicalize(a0_in);
    const CraftState b0 = canonicalize(b0_in);
    double arc = arc_s;
    if (arc > canon::SCREEN_ARC_MAX_S) { arc = canon::SCREEN_ARC_MAX_S; }
    if (arc <= 0.0) { return false; }

    // Altitude-band prefilter (SPEC §4.2a). J2 shifts the instantaneous
    // radius negligibly over the arc, so the two-body rings are a safe outer
    // bound; pad by the along-arc radial excursion is unnecessary at this
    // gate (R_SCREEN margin dominates).
    double alo, ahi, blo, bhi;
    detail::radial_bounds(a0, &alo, &ahi);
    detail::radial_bounds(b0, &blo, &bhi);
    const double gap = (alo > bhi) ? (alo - bhi) : (blo - ahi);
    if (gap > canon::R_SCREEN_M) { return false; }

    const auto g_at = [&a0, &b0](double t, Vec3* dr_out) {
        const CraftState sa = propagate(a0, t);
        const CraftState sb = propagate(b0, t);
        const Vec3 dr = sb.position - sa.position;
        const Vec3 dv = sb.velocity - sa.velocity;
        if (dr_out != nullptr) { *dr_out = dr; }
        return dr.dot(dv);
    };

    const auto finalize = [&](double t_star, OrbitConjunction* oc) {
        const double t_q = canon::quantize(t_star, canon::Q_TIME_S);
        oc->a_tca = canonicalize(propagate(a0, t_q));
        oc->b_tca = canonicalize(propagate(b0, t_q));
        Conjunction c;
        c.t_ca = t_q;
        c.miss_vector = oc->b_tca.position - oc->a_tca.position;
        c.rel_velocity = oc->b_tca.velocity - oc->a_tca.velocity;
        c.miss_distance = c.miss_vector.norm();
        c.detected = (c.miss_distance < canon::R_SCREEN_M);
        c.pc = 0.0;
        if (c.detected) {
            c.pc = starboard_pc(c, cov_quantize(cov_add(cov_a, cov_b)), r_hb);
        }
        oc->conj = c;
        return c.detected;
    };

    // t = 0: inside-bubble (already at/past closest approach but still inside
    // the screening radius).
    Vec3 dr0;
    const double g0 = g_at(0.0, &dr0);
    if ((g0 >= 0.0) && (dr0.norm() < canon::R_SCREEN_M)) {
        if (finalize(0.0, out)) { return true; }
    }

    // Grid walk: a range minimum lies in every step where g goes negative ->
    // non-negative. SCREEN_STEP_S is far below the relative-dynamics
    // timescale, so each step holds at most one extremum (SPEC §4.2a).
    double g_prev = g0;
    double t_prev = 0.0;
    const double step = canon::SCREEN_STEP_S;
    for (double t = step;; t = ((t + step) < arc) ? (t + step) : arc) {
        const double g_now = g_at(t, nullptr);
        if ((g_prev < 0.0) && (g_now >= 0.0)) {
            double lo = t_prev;
            double hi = t;
            for (int i = 0; i < canon::TCA_BISECT_ITERS; ++i) {
                const double mid = 0.5 * (lo + hi);
                if (g_at(mid, nullptr) < 0.0) { lo = mid; }
                else { hi = mid; }
            }
            if (finalize(0.5 * (lo + hi), out)) { return true; }
        }
        g_prev = g_now;
        t_prev = t;
        if (t >= arc) { break; }
    }
    return false;
}

} // namespace starboard
