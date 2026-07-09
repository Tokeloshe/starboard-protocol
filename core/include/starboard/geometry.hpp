// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — Layer 0: conjunction geometry (SPEC §4.1–§4.2).
//
// Everything here is computed from the RELATIVE state of two craft, quantized
// onto the canonical lattice, so both satellites derive identical bits from
// symmetric inputs. The verdict path through this header uses only IEEE-exact
// operations (+, -, *, /, sqrt).
// ============================================================================

#include "canon.hpp"

namespace starboard {

struct Vec3 {
    double x, y, z;

    Vec3() : x(0.0), y(0.0), z(0.0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    // Pinned expansion order (SPEC §3.5): x, then y, then z. No reassociation.
    double dot(const Vec3& o) const { return (x * o.x + y * o.y) + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double norm2() const { return dot(*this); }
    double norm() const { return std::sqrt(norm2()); }
    Vec3 normalized() const {
        const double n = norm();
        return (n > 0.0) ? (*this / n) : Vec3{};
    }
};

// Quantize a vector onto the canonical lattice (SPEC §3.4).
inline Vec3 quantize(const Vec3& v, double q) {
    return {canon::quantize(v.x, q), canon::quantize(v.y, q),
            canon::quantize(v.z, q)};
}

// Minimal orbital state: ECI (GCRF) position and velocity (SPEC §3.1).
struct CraftState {
    Vec3 position;   // m
    Vec3 velocity;   // m/s
};

// Snap a state onto the canonical lattice. All shared inputs MUST pass
// through this before any pairwise computation (SPEC §3.4).
inline CraftState canonicalize(const CraftState& s) {
    CraftState c;
    c.position = quantize(s.position, canon::Q_POS_M);
    c.velocity = quantize(s.velocity, canon::Q_VEL_MPS);
    return c;
}

// Result of Layer-0 screening between two craft (A = self, B = neighbour).
struct Conjunction {
    bool   detected;       // coarse gate: closest approach inside horizon & R_SCREEN
    double t_ca;           // time to closest approach from now (s); >= 0
    double miss_distance;  // predicted miss distance at TCA (m)
    Vec3   miss_vector;    // relative position of B w.r.t. A at TCA (m)
    Vec3   rel_velocity;   // velocity of B w.r.t. A (m/s), constant over screen
    double pc;             // Starboard-Pc; filled by assess_pc(), else 0
};

// Coarse screen of a single pair using locally-linear relative motion
// (SPEC §4.2). Symmetric under swap: screen(A,B) and screen(B,A) agree on
// t_ca and miss_distance bit-for-bit (the miss vector flips sign).
//
// Semantics (normative):
//  * t_raw <= 0        : the pair is at/past closest approach. t_ca := 0 and
//                        the pair is detected iff it is still inside R_SCREEN
//                        ("inside the bubble" — receding but not yet clear).
//  * t_raw >  horizon  : closest approach is beyond the window. NOT detected
//                        (the earlier draft clamped and mis-detected here).
//  * otherwise         : detected iff miss_distance < r_screen.
inline Conjunction screen(const CraftState& a, const CraftState& b,
                          double horizon_s,
                          double r_screen = canon::R_SCREEN_M) {
    const Vec3 dr = b.position - a.position;
    const Vec3 dv = b.velocity - a.velocity;
    const double dv2 = dv.dot(dv);

    Conjunction c;
    c.rel_velocity = dv;
    c.pc = 0.0;

    double t_raw;
    if (dv2 < 1.0e-12) {
        t_raw = 0.0; // co-drifting: closest approach is now
    } else {
        t_raw = -(dr.dot(dv)) / dv2;
    }

    if (t_raw <= 0.0) {
        c.t_ca = 0.0;
        c.miss_vector = dr;
    } else {
        c.t_ca = t_raw;
        c.miss_vector = dr + dv * t_raw;
    }
    c.miss_distance = c.miss_vector.norm();
    c.detected = (t_raw <= horizon_s) && (c.miss_distance < r_screen);
    return c;
}

} // namespace starboard
