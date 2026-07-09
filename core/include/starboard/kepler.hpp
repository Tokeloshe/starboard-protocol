// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — the canonical ephemeris (SPEC §4.2).
//
// Two-body Keplerian propagation via Lagrange f&g functions, used as the
// SHARED interpolant between catalog states: given the same canonical state,
// every conformant implementation computes the identical propagated state,
// bit for bit. Three ingredients make that true:
//
//   1. The eccentric-anomaly difference form of Kepler's equation
//          F(dE) = dE + esE0*(1 - cos dE) - ecE0*sin dE - n*dt = 0
//      needs only e*sin(E0) and e*cos(E0), which come from the state
//      WITHOUT any atan2.
//   2. The solver is FIXED-ITERATION: 48 bisections (F is strictly
//      increasing — F' = r/a > 0 — so bisection provably converges for every
//      eccentricity e < 1) followed by 3 Newton polish steps. No
//      convergence-dependent branching, no early exit: identical inputs take
//      the identical arithmetic path on every machine.
//   3. All trigonometry is canon_sincos (SPEC §4.6).
//
// Fidelity note (SPEC §4.2): two-body is the canonical *agreement basis*, not
// a precision ephemeris. Catalog refresh cadence bounds its error inside the
// covariance; higher-fidelity private ephemerides MAY inform operator alerts
// but MUST NOT feed the shared verdict. Non-elliptic states (alpha <= 0,
// e.g. hyperbolic debris) are outside SB/1 and propagate linearly, which is
// conservative over a screening window.
// ============================================================================

#include "geometry.hpp"

namespace starboard {

inline CraftState kepler_propagate(const CraftState& s0, double dt) {
    const double r0n = s0.position.norm();
    const double v02 = s0.velocity.dot(s0.velocity);
    const double alpha = 2.0 / r0n - v02 / canon::MU_EARTH_M3S2;   // = 1/a

    if (alpha <= 0.0) {
        // Non-elliptic: out of SB/1 domain — linear fallback (conservative).
        CraftState lin;
        lin.position = s0.position + s0.velocity * dt;
        lin.velocity = s0.velocity;
        return lin;
    }

    const double a = 1.0 / alpha;
    const double n = std::sqrt(canon::MU_EARTH_M3S2 / ((a * a) * a));
    const double sqrt_mua = std::sqrt(canon::MU_EARTH_M3S2 * a);
    const double esE0 = s0.position.dot(s0.velocity) / sqrt_mua; // e*sin(E0)
    const double ecE0 = 1.0 - r0n / a;                           // e*cos(E0)

    // Reduce the mean-anomaly advance to [-pi, pi], tracking revolutions.
    constexpr double INV_TWOPI = 0.15915494309189535;         // pinned literal
    constexpr double TWOPI_HI  = 6.283185243606567;           // 0x401921FB50000000
    constexpr double TWOPI_MED = 6.357301884918343e-08;       // 0x3E7110B460000000
    constexpr double TWOPI_LO  = 2.4492935982947064e-16;      // 0x3CB1A62633145C07
    const double m_total = n * dt;
    const double kd = std::nearbyint(m_total * INV_TWOPI);    // revolutions
    const double m = ((m_total - kd * TWOPI_HI) - kd * TWOPI_MED) - kd * TWOPI_LO;

    // Solve F(dE) = dE + esE0*(1-cos dE) - ecE0*sin dE - m = 0 on the
    // principal branch. |dE - m| <= 3e < 3, so the root is bracketed by
    // [m - 3, m + 3]. F is strictly increasing => bisection cannot fail.
    const auto f_of = [esE0, ecE0, m](double de, double* fp) {
        double sd, cd;
        canon::canon_sincos(de, &sd, &cd);
        *fp = (1.0 + esE0 * sd) - ecE0 * cd;                  // F' = r/a > 0
        return ((de + esE0 * (1.0 - cd)) - ecE0 * sd) - m;
    };
    double lo = m - 3.0;
    double hi = m + 3.0;
    double de = m;
    double fp = 1.0;
    for (int i = 0; i < canon::KEPLER_BISECT_ITERS; ++i) {
        de = 0.5 * (lo + hi);
        const double fv = f_of(de, &fp);
        if (fv > 0.0) { hi = de; } else { lo = de; }
    }
    for (int i = 0; i < canon::KEPLER_POLISH_ITERS; ++i) {
        const double fv = f_of(de, &fp);
        de = de - fv / fp;
    }

    // Lagrange f & g (sin/cos of dE are periodic; the secular dE - sin dE
    // term needs the full revolution count back).
    double sd, cd;
    canon::canon_sincos(de, &sd, &cd);
    const double de_full = de + kd * TWOPI_HI + kd * (TWOPI_MED + TWOPI_LO);
    const double f = 1.0 - (a / r0n) * (1.0 - cd);
    const double g = dt - (de_full - sd) / n;

    CraftState s;
    s.position = s0.position * f + s0.velocity * g;
    const double rn = s.position.norm();
    const double fdot = -(sqrt_mua / (r0n * rn)) * sd;
    const double gdot = 1.0 - (a / rn) * (1.0 - cd);
    s.velocity = s0.position * fdot + s0.velocity * gdot;
    return s;
}

} // namespace starboard
