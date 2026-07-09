// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Shared host-side test/demo support: the ONE C++ definition of the scenario
// geometry helpers, the deterministic LCG, and the bit-pattern extractor.
// Used by the conformance runner, the demo, and the validation tools —
// previously each carried its own copy, which is exactly how the conformance
// anchors and the demo geometry drift apart. (The C99 port's copies in
// ports/c/main.c are DELIBERATE independent re-implementations for the
// cross-language bit-agreement proof and must stay separate.)
//
// Host tooling only — never included by flight code. libm sin/cos here is
// fine: scenario CONSTRUCTION is off the verdict path (SPEC §3.5).
// ============================================================================

#include "starboard/starboard.hpp"

#include <cmath>
#include <cstring>

namespace sbtest {

// IEEE-754 bit pattern of a double (memcpy: the alias-safe type pun).
inline uint64_t bits_of(double x) {
    uint64_t u;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

// A craft on a circular orbit of radius r, in the plane produced by rotating
// the equatorial plane by `incl` about the +x axis, at phase angle `theta`
// (theta = 0 is the node on +x).
inline starboard::CraftState circular(double r, double incl, double theta) {
    const double v = std::sqrt(starboard::canon::MU_EARTH_M3S2 / r);
    const double ct = std::cos(theta), st = std::sin(theta);
    const double ci = std::cos(incl), si = std::sin(incl);
    starboard::CraftState s;
    s.position = starboard::Vec3(r * ct, r * st * ci, r * st * si);
    s.velocity = starboard::Vec3(-v * st, v * ct * ci, v * ct * si);
    return s;
}

// Build a craft that crosses `ref` at time tc, `above` metres higher, with
// velocity tilted `tilt` rad out of ref's plane — then back-propagate it to
// the shared epoch. Because the construction uses the canonical (J2)
// propagator on the canonicalized reference, the designed miss holds under
// the canonical screening dynamics. This is the geometry behind the demo's
// encounters AND conformance SB-007/SB-013 — one definition, no drift.
inline starboard::CraftState crossing_against(const starboard::CraftState& ref,
                                              double tc, double above,
                                              double tilt) {
    using namespace starboard;
    const CraftState rc = propagate(canonicalize(ref), tc);
    const Vec3 rhat = rc.position.normalized();
    const Vec3 vhat = rc.velocity.normalized();
    const Vec3 chat = rhat.cross(vhat).normalized();
    const double r_o = rc.position.norm() + above;
    const double v_o = std::sqrt(canon::MU_EARTH_M3S2 / r_o);
    CraftState cross;
    cross.position = rhat * r_o;
    cross.velocity = (vhat * std::cos(tilt) + chat * std::sin(tilt)) * v_o;
    return propagate(cross, -tc);
}

// The deterministic LCG all test tooling draws from (fixed constants; no
// time/seed nondeterminism anywhere in the suite).
inline uint64_t lcg_next(uint64_t s) {
    return s * 6364136223846793005ull + 1442695040888963407ull;
}

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    // Uniform in [0, 1).
    double uniform() {
        s = lcg_next(s);
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
    // Uniform in [-1, 1).
    double sym() { return 2.0 * uniform() - 1.0; }
};

} // namespace sbtest
