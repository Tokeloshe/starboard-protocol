// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — the canonical propagator (SPEC §4.2).
//
// A fixed-step RK4 integration of two-body + J2 zonal dynamics. This is the
// SHARED interpolant between the reference-epoch state (from a conjunction
// data message, SPEC §3.3) and the screening arc: given the same canonical
// state, every conformant implementation computes the identical propagated
// state, bit for bit. Determinism rests on three pinned choices:
//
//   1. The acceleration is purely algebraic (two-body + J2 zonal) — only
//      +, -, *, /, sqrt, all IEEE-correctly-rounded. NO transcendental is
//      reachable on this path (unlike an analytic Keplerian solve, which
//      needs sin/cos/atan2). The verdict path's only pinned transcendental
//      is therefore canon_exp, in Pc.
//   2. Fixed step size PROP_STEP_S with a deterministic step count:
//      n_full = floor(dt / PROP_STEP_S) whole steps of PROP_STEP_S, then one
//      partial step of the remainder. Whole steps are identical regardless of
//      the target time, so an implementation MAY march incrementally and
//      still bit-match a from-epoch re-integration (the flight optimization).
//   3. Pinned RK4 stage order and pinned intra-vector expansion order.
//
// Why J2 and why bounded: two-body alone diverges from real LEO motion by
// kilometres within minutes (J2 nodal regression dominates). With J2 in the
// propagator and the arc bounded to SCREEN_ARC_MAX_S, the residual vs a
// high-fidelity truth (drag, higher zonals) is order 1 m — a small fraction
// of R_SAFE and well inside any operational covariance (SPEC §4.2 error
// budget; validated in conformance/validation). Drag is deliberately NOT in
// the canonical model: it depends on un-pinnable atmosphere and vehicle
// parameters, so folding its (bounded) effect into the covariance keeps the
// shared computation deterministic.
//
// Propagation is forward only (dt >= 0): screening runs over [epoch, epoch +
// arc]. Allocation-free, recursion-free, exception-free.
// ============================================================================

#include "geometry.hpp"

namespace starboard {

// Two-body + J2 zonal acceleration at an ECI position (SPEC §4.2). Pinned
// expansion order. Uses only algebraic ops + sqrt (via norm()).
inline Vec3 accel_j2(const Vec3& r) {
    const double r2 = r.norm2();
    const double rn = std::sqrt(r2);
    const double inv_r3 = 1.0 / (r2 * rn);
    // Two-body.
    Vec3 a = r * (-canon::MU_EARTH_M3S2 * inv_r3);
    // J2 zonal. f = -3/2 * J2 * mu * Re^2 / r^5; z-term carries +2 (the
    // 3 - 5(z/r)^2 vs 1 - 5(z/r)^2 split).
    const double zr2 = (r.z * r.z) / r2;
    const double re2 = canon::R_EARTH_M * canon::R_EARTH_M;
    const double f = (-1.5 * canon::J2_EARTH * canon::MU_EARTH_M3S2 * re2) *
                     (inv_r3 / r2);
    const double c_xy = 1.0 - 5.0 * zr2;
    const double c_z = 3.0 - 5.0 * zr2;
    a.x += f * r.x * c_xy;
    a.y += f * r.y * c_xy;
    a.z += f * r.z * c_z;
    return a;
}

namespace detail {
// One classical RK4 step of size h for [r; v] under accel_j2. Pinned stage
// order and pinned combination order.
inline void rk4_step(CraftState& s, double h) {
    const Vec3 k1v = s.velocity;
    const Vec3 k1a = accel_j2(s.position);
    const Vec3 k2v = s.velocity + k1a * (0.5 * h);
    const Vec3 k2a = accel_j2(s.position + k1v * (0.5 * h));
    const Vec3 k3v = s.velocity + k2a * (0.5 * h);
    const Vec3 k3a = accel_j2(s.position + k2v * (0.5 * h));
    const Vec3 k4v = s.velocity + k3a * h;
    const Vec3 k4a = accel_j2(s.position + k3v * h);
    const double h6 = h / 6.0;
    s.position = s.position + ((k1v + k4v) + (k2v + k3v) * 2.0) * h6;
    s.velocity = s.velocity + ((k1a + k4a) + (k2a + k3a) * 2.0) * h6;
}
} // namespace detail

// Propagate a canonical state by dt (SPEC §4.2). Screening uses dt >= 0;
// dt < 0 (backward) is equally valid and deterministic (RK4 is symmetric in
// the step sign). Deterministic step decomposition: floor(|dt| / PROP_STEP_S)
// whole steps of the signed PROP_STEP_S, then one partial step.
inline CraftState propagate(const CraftState& s0, double dt) {
    CraftState s = s0;
    if (dt == 0.0) { return s; }
    const double sign = (dt < 0.0) ? -1.0 : 1.0;
    const double adt = sign * dt;
    const double h = sign * canon::PROP_STEP_S;
    // adt is bounded (<= arc, so <= ~256 steps); the cast is exact-floor.
    const long n_full = static_cast<long>(adt / canon::PROP_STEP_S);
    for (long i = 0; i < n_full; ++i) {
        detail::rk4_step(s, h);
    }
    const double rem = adt - static_cast<double>(n_full) * canon::PROP_STEP_S;
    if (rem > 0.0) {
        detail::rk4_step(s, sign * rem);
    }
    return s;
}

} // namespace starboard
