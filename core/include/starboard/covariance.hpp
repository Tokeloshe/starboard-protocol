// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — covariance-aware probability of collision (SPEC §4.3–§4.6).
//
// Starboard-Pc is the standard short-encounter 2-D formulation: the combined
// position covariance is projected onto the encounter plane, and the Gaussian
// is integrated over the combined hard-body disc. It is a DEFINED quantity,
// not an approximation: conformance level SB-C2 requires bit-equality.
//
// The disc integral is evaluated by PATERA'S divergence-theorem reduction to
// a 1-D CONTOUR integral, sampled at a FIXED PATERA_NODES equally-spaced
// angles (periodic trapezoid on an analytic periodic integrand => spectral
// convergence). This replaces the earlier Chan equivalent-area series, whose
// circularization error reached tens of percent for elongated/correlated
// covariance — the common LEO case. Patera's error is set by the node count,
// NOT the covariance shape: N=32 matches an independent adaptive quadrature to
// ~1e-12 relative for aspect ratios up to 50:1 (conformance/validation).
//
// No eigen-rotation and no atan2: the plane is whitened with the Cholesky
// factor of C2^{-1} (sqrt only), and the standard normal's rotational
// invariance makes any such whitening valid. The verdict path uses only
// IEEE-exact ops (+ - * / sqrt) plus the two pinned transcendentals
// canon_exp and canon_sincos, so it is bit-deterministic across
// implementations. (This is the one place canon_sincos is on the VERDICT
// path — SPEC §4.6.)
// ============================================================================

#include "geometry.hpp"

namespace starboard {

// 3x3 symmetric positive-semidefinite matrix (position covariance, m^2, ECI).
struct Sym33 {
    double xx, xy, xz, yy, yz, zz;

    static Sym33 diagonal(double v) { return {v, 0.0, 0.0, v, 0.0, v}; }
    static Sym33 zero() { return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; }
};

inline Sym33 cov_add(const Sym33& a, const Sym33& b) {
    return {a.xx + b.xx, a.xy + b.xy, a.xz + b.xz,
            a.yy + b.yy, a.yz + b.yz, a.zz + b.zz};
}

inline Sym33 cov_quantize(const Sym33& c) {
    return {canon::quantize(c.xx, canon::Q_COV_M2),
            canon::quantize(c.xy, canon::Q_COV_M2),
            canon::quantize(c.xz, canon::Q_COV_M2),
            canon::quantize(c.yy, canon::Q_COV_M2),
            canon::quantize(c.yz, canon::Q_COV_M2),
            canon::quantize(c.zz, canon::Q_COV_M2)};
}

// THE canonical covariance-combination policy (SPEC §4.3), defined once:
// quantized sum of the two published covariances. Every entry point into
// Starboard-Pc MUST combine through this helper so a future policy change
// (e.g. a PSD floor after combining) lands everywhere at once.
inline Sym33 combined_covariance(const Sym33& cov_a, const Sym33& cov_b) {
    return cov_quantize(cov_add(cov_a, cov_b));
}

// u' C v with pinned expansion order (SPEC §4.4).
inline double cov_quad(const Sym33& c, const Vec3& u, const Vec3& v) {
    const double row_x = (c.xx * v.x + c.xy * v.y) + c.xz * v.z;
    const double row_y = (c.xy * v.x + c.yy * v.y) + c.yz * v.z;
    const double row_z = (c.xz * v.x + c.yz * v.y) + c.zz * v.z;
    return (u.x * row_x + u.y * row_y) + u.z * row_z;
}

namespace detail {
// Pinned perpendicular fallback (SPEC §4.4): the unit vector perpendicular to
// n obtained by Gram-Schmidt from the coordinate axis with the smallest
// |component| of n (ties resolved x before y before z).
inline Vec3 pinned_perpendicular(const Vec3& n) {
    const double ax = (n.x < 0.0) ? -n.x : n.x;
    const double ay = (n.y < 0.0) ? -n.y : n.y;
    const double az = (n.z < 0.0) ? -n.z : n.z;
    Vec3 axis(1.0, 0.0, 0.0);
    if ((ay < ax) && (ay <= az)) { axis = Vec3(0.0, 1.0, 0.0); }
    else if ((az < ax) && (az < ay)) { axis = Vec3(0.0, 0.0, 1.0); }
    const Vec3 p = axis - n * axis.dot(n);
    return p.normalized();
}
} // namespace detail

// Starboard-Pc (SPEC §4.5). Inputs MUST already be canonical:
//   conj   — from screen() on canonicalized states
//   c_comb — quantized combined covariance C_A + C_B (ECI, m^2)
//   r_hb   — combined hard-body radius (m)
// Symmetric under swap: every term depends on squares/quadratics of the
// relative geometry, so Pc(A,B) and Pc(B,A) are bit-identical.
inline double starboard_pc(const Conjunction& conj, const Sym33& c_comb,
                           double r_hb) {
    // --- Encounter-plane basis (SPEC §4.4) ---
    // Co-drifting pairs have no encounter plane: Pc degenerates to the
    // hard-body containment test (the R_SAFE floor in actionable() still
    // applies regardless).
    const double vmag = conj.rel_velocity.norm();
    if (vmag < 1.0e-9) {
        return (conj.miss_distance < r_hb) ? 1.0 : 0.0;
    }
    const Vec3 n = conj.rel_velocity / vmag;
    const double d = conj.miss_distance;
    const Vec3 e1_raw = (d > canon::Q_POS_M) ? (conj.miss_vector / d)
                                             : detail::pinned_perpendicular(n);
    // Gram-Schmidt against n: at an interior TCA the miss vector is already
    // perpendicular to v_rel; at a clamped TCA (t_ca = 0) it need not be.
    const Vec3 e1_gs = e1_raw - n * e1_raw.dot(n);
    const double e1_gs_n = e1_gs.norm();
    const Vec3 e1 = (e1_gs_n > 1.0e-9) ? (e1_gs / e1_gs_n)
                                       : detail::pinned_perpendicular(n);
    const Vec3 e2 = n.cross(e1);
    // In-plane miss: by construction the miss vector lies in span(n, e1), so
    // its plane coordinates are (d_p, 0) with d_p = miss . e1.
    const double d_p = conj.miss_vector.dot(e1);

    // --- Project the combined covariance into the plane ---
    double a  = cov_quad(c_comb, e1, e1);
    double b  = cov_quad(c_comb, e2, e2);
    double cx = cov_quad(c_comb, e1, e2);
    if (a < canon::VAR_FLOOR_M2) { a = canon::VAR_FLOOR_M2; }
    if (b < canon::VAR_FLOOR_M2) { b = canon::VAR_FLOOR_M2; }
    // PSD repair (SPEC §4.4): a malformed or hostile covariance can project
    // to |cx| with cx^2 > a*b (not positive-semidefinite). Without repair the
    // Cholesky below takes sqrt of a negative number, Pc becomes NaN, and a
    // NaN silently fails the actionability threshold — a bad covariance
    // would DISABLE the gate. Canonical repair: drop the correlation.
    if (cx * cx > a * b) { cx = 0.0; }
    double det = a * b - cx * cx;
    const double det_floor = canon::VAR_FLOOR_M2 * canon::VAR_FLOOR_M2;
    if (det < det_floor) { det = det_floor; }

    // --- Whitening: Cholesky L Lᵀ = C2⁻¹, so uᵀu = sᵀC2⁻¹s under u = Lᵀs.
    // C2⁻¹ = (1/det) [[b, -c],[-c, a]]; sqrt only, no eigen-rotation. ---
    const double inv = 1.0 / det;
    const double L11 = std::sqrt(b * inv);
    const double L21 = (-cx * inv) / L11;
    const double L22 = std::sqrt(a * inv - L21 * L21);

    // --- Patera contour integral (SPEC §4.5): the collision disc is centered
    // at s_c = (-d_p, 0) with radius r_hb; the standard-normal probability is
    // P = (1/2pi) oint (1 - e^{-|u|^2/2})/|u|^2 (u_x du_y - u_y du_x), sampled
    // at PATERA_NODES equally-spaced angles. Pinned ascending summation. ---
    const double R = r_hb;
    const double two_pi = 2.0 * canon::PI;
    const double dphi = two_pi / static_cast<double>(canon::PATERA_NODES);
    double sum = 0.0;
    for (int i = 0; i < canon::PATERA_NODES; ++i) {
        double cphi, sphi;
        canon::canon_sincos(dphi * static_cast<double>(i), &sphi, &cphi);
        const double sx = -d_p + R * cphi;   // boundary point (s-space)
        const double sy = R * sphi;
        const double dsx = -R * sphi;         // d/dphi of the boundary point
        const double dsy = R * cphi;
        const double ux = L11 * sx + L21 * sy;   // whitened coordinates
        const double uy = L22 * sy;
        const double dux = L11 * dsx + L21 * dsy;
        const double duy = L22 * dsy;
        const double w = ux * ux + uy * uy;      // |u|^2
        const double cross = ux * duy - uy * dux;
        double factor;
        if (w < canon::Q_COV_M2) {   // 2^-20: series near the removable pole
            factor = 0.5 - 0.125 * w;            // (1-e^{-w/2})/w = 1/2 - w/8 …
        } else {
            factor = (1.0 - canon::canon_exp(-0.5 * w)) / w;
        }
        sum += factor * cross;
    }
    // P = (1/2pi) * dphi * sum = sum / PATERA_NODES (exact: N is 2^5).
    double pc = sum / static_cast<double>(canon::PATERA_NODES);
    if (pc < 0.0) { pc = 0.0; }
    if (pc > 1.0) { pc = 1.0; }
    return pc;
}

// Layer-0 actionability (SPEC §4.7): a detected conjunction is ACTIONABLE —
// i.e. Layer 1 MUST be evaluated — iff Pc >= PC_ACTIONABLE or the predicted
// miss is already inside the required safe radius (covariance optimism is
// never allowed to waive a sub-R_SAFE miss).
inline bool actionable(const Conjunction& conj) {
    return conj.detected &&
           ((conj.pc >= canon::PC_ACTIONABLE) ||
            (conj.miss_distance < canon::R_SAFE_M));
}

// Convenience: screen + Pc in one call, with conservative covariance defaults
// (a craft that publishes no covariance is assigned DEFAULT_VAR_M2 isotropic).
inline Conjunction assess(const CraftState& a, const CraftState& b,
                          const Sym33& cov_a, const Sym33& cov_b,
                          double r_hb, double horizon_s) {
    Conjunction c = screen(canonicalize(a), canonicalize(b), horizon_s);
    if (c.detected) {
        c.pc = starboard_pc(c, combined_covariance(cov_a, cov_b), r_hb);
    }
    return c;
}

} // namespace starboard
