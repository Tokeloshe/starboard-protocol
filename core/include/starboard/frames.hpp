// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — Hill (RIC) frame and Clohessy-Wiltshire dynamics (SPEC §6.2–§6.3).
//
// Frame convention (normative): x = radial (zenith), y = along-track
// (completing the right-handed triad, ~velocity for near-circular orbits),
// z = cross-track (orbit normal). "Hill frame of craft X" means this triad
// evaluated at X's canonical state.
//
// The CW state transition matrix is used for maneuver planning and
// return-to-slot (Layer 3). Maneuver quantities are NOT on the bit-exact
// verdict path (SPEC §3.5): sin/cos are permitted here, and conformance
// level SB-C2 checks them to a relative tolerance instead of bit-equality.
// ============================================================================

#include "geometry.hpp"

namespace starboard {

struct Mat3 {
    double m[3][3];

    static Mat3 identity() {
        return {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    }

    Vec3 mul(const Vec3& v) const {
        return {(m[0][0] * v.x + m[0][1] * v.y) + m[0][2] * v.z,
                (m[1][0] * v.x + m[1][1] * v.y) + m[1][2] * v.z,
                (m[2][0] * v.x + m[2][1] * v.y) + m[2][2] * v.z};
    }

    // Transpose-multiply: for an orthonormal basis matrix this is the inverse.
    Vec3 tmul(const Vec3& v) const {
        return {(m[0][0] * v.x + m[1][0] * v.y) + m[2][0] * v.z,
                (m[0][1] * v.x + m[1][1] * v.y) + m[2][1] * v.z,
                (m[0][2] * v.x + m[1][2] * v.y) + m[2][2] * v.z};
    }
};

// Solve A x = b by the analytic adjugate (no pivoting, no allocation).
// Callers guarantee A is well-conditioned (SPEC §6.5 pins transfer times
// away from CW singularities).
inline Vec3 mat3_solve(const Mat3& a, const Vec3& b) {
    const double c00 = a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1];
    const double c01 = a.m[1][2] * a.m[2][0] - a.m[1][0] * a.m[2][2];
    const double c02 = a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0];
    const double det = a.m[0][0] * c00 + a.m[0][1] * c01 + a.m[0][2] * c02;
    const double inv = 1.0 / det;
    const double c10 = a.m[0][2] * a.m[2][1] - a.m[0][1] * a.m[2][2];
    const double c11 = a.m[0][0] * a.m[2][2] - a.m[0][2] * a.m[2][0];
    const double c12 = a.m[0][1] * a.m[2][0] - a.m[0][0] * a.m[2][1];
    const double c20 = a.m[0][1] * a.m[1][2] - a.m[0][2] * a.m[1][1];
    const double c21 = a.m[0][2] * a.m[1][0] - a.m[0][0] * a.m[1][2];
    const double c22 = a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0];
    return {(c00 * b.x + c10 * b.y + c20 * b.z) * inv,
            (c01 * b.x + c11 * b.y + c21 * b.z) * inv,
            (c02 * b.x + c12 * b.y + c22 * b.z) * inv};
}

// ECI -> Hill rotation for a craft state. Rows are the RIC unit vectors, so
// x_hill = basis.mul(x_eci) and x_eci = basis.tmul(x_hill).
inline Mat3 hill_basis(const CraftState& s) {
    const Vec3 rhat = s.position.normalized();
    const Vec3 chat = s.position.cross(s.velocity).normalized();
    const Vec3 ihat = chat.cross(rhat);
    return {{{rhat.x, rhat.y, rhat.z},
             {ihat.x, ihat.y, ihat.z},
             {chat.x, chat.y, chat.z}}};
}

// Canonical mean motion (SPEC §6.2): the reference orbit is the circular
// orbit at the craft's quantized geocentric radius. Both craft can reproduce
// this from the shared state — no private elements involved.
inline double mean_motion(const CraftState& s) {
    const double a = canon::quantize(s.position.norm(), canon::Q_POS_M);
    return std::sqrt(canon::MU_EARTH_M3S2 / ((a * a) * a));
}

// --- Clohessy-Wiltshire state-transition blocks (SPEC §6.3) -----------------
// State [r; v] in the Hill frame of the reference orbit, mean motion n:
//   [r(t); v(t)] = [ phi_rr  phi_rv ; phi_vr  phi_vv ] [r0; v0]

inline Mat3 cw_phi_rr(double n, double t) {
    const double s = std::sin(n * t), c = std::cos(n * t);
    return {{{4.0 - 3.0 * c,        0.0, 0.0},
             {6.0 * (s - n * t),    1.0, 0.0},
             {0.0,                  0.0, c}}};
}

inline Mat3 cw_phi_rv(double n, double t) {
    const double s = std::sin(n * t), c = std::cos(n * t);
    return {{{s / n,                 2.0 * (1.0 - c) / n,        0.0},
             {-2.0 * (1.0 - c) / n,  (4.0 * s - 3.0 * n * t) / n, 0.0},
             {0.0,                   0.0,                         s / n}}};
}

inline Mat3 cw_phi_vr(double n, double t) {
    const double s = std::sin(n * t), c = std::cos(n * t);
    return {{{3.0 * n * s,          0.0, 0.0},
             {-6.0 * n * (1.0 - c), 0.0, 0.0},
             {0.0,                  0.0, -n * s}}};
}

inline Mat3 cw_phi_vv(double n, double t) {
    const double s = std::sin(n * t), c = std::cos(n * t);
    return {{{c,        2.0 * s,       0.0},
             {-2.0 * s, 4.0 * c - 3.0, 0.0},
             {0.0,      0.0,           c}}};
}

// Relative state in the Hill frame (offset from an assigned slot).
struct HillState {
    Vec3 r;   // m
    Vec3 v;   // m/s
};

inline HillState cw_propagate(const HillState& x0, double n, double t) {
    HillState x;
    x.r = cw_phi_rr(n, t).mul(x0.r) + cw_phi_rv(n, t).mul(x0.v);
    x.v = cw_phi_vr(n, t).mul(x0.r) + cw_phi_vv(n, t).mul(x0.v);
    return x;
}

} // namespace starboard
