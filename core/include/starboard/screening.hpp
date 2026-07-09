// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — canonical conjunction search (SPEC §4.2a).
//
// The end-to-end Layer-0 screener: propagate both craft along the canonical
// ephemeris, find the range-rate sign changes on the canonical 32 s grid,
// refine each candidate TCA with a fixed 48-step bisection (the sign of
// g(t) = dr·dv is a shared, bit-deterministic quantity, so every
// implementation walks the identical bracket sequence), then evaluate the
// encounter kernel — miss geometry and Starboard-Pc — at the refined TCA.
//
// The linear screen() in geometry.hpp remains the *encounter kernel* (its
// inside-bubble semantics also cover the t=0 co-drifting case here); this
// header is the normative way to FIND the encounter.
//
// An altitude-band prefilter (periapsis/apoapsis rings farther apart than
// R_SCREEN can never conjunct) rejects the overwhelming majority of catalog
// pairs with two square roots and no propagation.
// ============================================================================

#include "covariance.hpp"
#include "kepler.hpp"

namespace starboard {

// A found conjunction plus the canonical states at TCA — Layer 1 evaluates
// rule geometry AT the encounter (SPEC §5.1), so the screener returns it.
struct OrbitConjunction {
    Conjunction conj;        // t_ca relative to the shared state epoch (s)
    CraftState  a_tca;       // canonical state of A at TCA
    CraftState  b_tca;       // canonical state of B at TCA
};

namespace detail {

// Radial bounds [periapsis, apoapsis] from a canonical state — no atan2:
// e^2 = (e sinE0)^2 + (e cosE0)^2.
inline void radial_bounds(const CraftState& s, double* r_lo, double* r_hi) {
    const double rn = s.position.norm();
    const double v2 = s.velocity.dot(s.velocity);
    const double alpha = 2.0 / rn - v2 / canon::MU_EARTH_M3S2;
    if (alpha <= 0.0) {
        *r_lo = 0.0;
        *r_hi = 1.0e18;   // unbounded (non-elliptic): never prefiltered out
        return;
    }
    const double a = 1.0 / alpha;
    const double esE0 = s.position.dot(s.velocity) /
                        std::sqrt(canon::MU_EARTH_M3S2 * a);
    const double ecE0 = 1.0 - rn / a;
    const double e = std::sqrt(esE0 * esE0 + ecE0 * ecE0);
    *r_lo = a * (1.0 - e);
    *r_hi = a * (1.0 + e);
}

} // namespace detail

// Canonical pairwise search over [0, horizon_s]. Returns true and fills
// `out` with the EARLIEST detected conjunction; false if the pair is clean.
// Inputs are canonicalized internally; outputs are canonical.
inline bool screen_orbits(const CraftState& a0_in, const CraftState& b0_in,
                          const Sym33& cov_a, const Sym33& cov_b,
                          double r_hb, double horizon_s,
                          OrbitConjunction* out) {
    const CraftState a0 = canonicalize(a0_in);
    const CraftState b0 = canonicalize(b0_in);

    // Altitude-band prefilter (SPEC §4.2a): disjoint radial rings by more
    // than R_SCREEN can never come within R_SCREEN.
    double alo, ahi, blo, bhi;
    detail::radial_bounds(a0, &alo, &ahi);
    detail::radial_bounds(b0, &blo, &bhi);
    const double gap = (alo > bhi) ? (alo - bhi) : (blo - ahi);
    if (gap > canon::R_SCREEN_M) { return false; }

    // g(t) = dr.dv — the range-rate sign function on the canonical grid.
    const auto g_at = [&a0, &b0](double t, Vec3* dr_out, Vec3* dv_out) {
        const CraftState sa = kepler_propagate(a0, t);
        const CraftState sb = kepler_propagate(b0, t);
        const Vec3 dr = sb.position - sa.position;
        const Vec3 dv = sb.velocity - sa.velocity;
        if (dr_out != nullptr) { *dr_out = dr; }
        if (dv_out != nullptr) { *dv_out = dv; }
        return dr.dot(dv);
    };

    // Finalize a candidate TCA: canonical states, kernel, Pc.
    const auto finalize = [&](double t_star, OrbitConjunction* oc) {
        const double t_q = canon::quantize(t_star, canon::Q_TIME_S);
        oc->a_tca = canonicalize(kepler_propagate(a0, t_q));
        oc->b_tca = canonicalize(kepler_propagate(b0, t_q));
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

    // t = 0: inside-bubble semantics (already at/past closest approach but
    // still inside the screening radius).
    Vec3 dr0;
    double g_prev = g_at(0.0, &dr0, nullptr);
    if ((g_prev >= 0.0) && (dr0.norm() < canon::R_SCREEN_M)) {
        if (finalize(0.0, out)) { return true; }
    }

    // Canonical grid walk: a range minimum lies in every step where g goes
    // negative -> non-negative. SCREEN_STEP_S is chosen well below the
    // shortest relative-dynamics timescale, so each step holds at most one
    // extremum (SPEC §4.2a).
    double t_prev = 0.0;
    const double step = canon::SCREEN_STEP_S;
    for (double t = step; t_prev < horizon_s;
         t = ((t + step) < horizon_s) ? (t + step) : horizon_s) {
        const double g_now = g_at(t, nullptr, nullptr);
        if ((g_prev < 0.0) && (g_now >= 0.0)) {
            double lo = t_prev;
            double hi = t;
            for (int i = 0; i < canon::TCA_BISECT_ITERS; ++i) {
                const double mid = 0.5 * (lo + hi);
                if (g_at(mid, nullptr, nullptr) < 0.0) { lo = mid; }
                else { hi = mid; }
            }
            if (finalize(0.5 * (lo + hi), out)) { return true; }
        }
        g_prev = g_now;
        t_prev = t;
        if (t >= horizon_s) { break; }
    }
    return false;
}

} // namespace starboard
