// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — canonical constants, quantization lattice, and the one pinned
// transcendental (SPEC §3–§4).
//
// Everything in this header is NORMATIVE. Two independent implementations
// that quantize their shared inputs onto this lattice and evaluate the
// verdict path with the Strict Arithmetic Profile (SPEC §3.5) produce
// BIT-IDENTICAL results. Changing any value here places an implementation
// off-network: its conjunction ids, screening decisions and verdicts no
// longer agree with the installed base (SPEC §11).
//
// Design notes for auditors:
//  * All quanta and most thresholds are exact powers of two, so every
//    lattice point is exactly representable in IEEE-754 binary64 and
//    quantization is idempotent.
//  * The verdict path uses only +, -, *, /, sqrt (all correctly rounded by
//    IEEE-754) and canon_exp() below (pinned by construction). There is no
//    sin/cos/atan2 anywhere a verdict depends on — by design (SPEC §4.4).
// ============================================================================

#include <cmath>
#include <cstdint>

namespace starboard {
namespace canon {

// --- §3.4 Quantization lattice (exact powers of two) -----------------------
constexpr double Q_POS_M   = 0.0009765625;        // 2^-10 m   (~1 mm)
constexpr double Q_VEL_MPS = 9.5367431640625e-07; // 2^-20 m/s (~1 um/s)
constexpr double Q_COV_M2  = 9.5367431640625e-07; // 2^-20 m^2
constexpr double Q_TIME_S  = 0.0009765625;        // 2^-10 s

// --- §3.6 Canonical physical constants --------------------------------------
constexpr double MU_EARTH_M3S2 = 3.986004418e14;  // EGM-96 GM, pinned literal
constexpr double PI = 3.141592653589793;          // pinned literal

// --- §4 Screening & actionability -------------------------------------------
constexpr double R_SCREEN_M    = 8192.0;   // 2^13 m coarse screening gate
constexpr double R_SAFE_M      = 1024.0;   // 2^10 m required post-maneuver miss
constexpr double PC_ACTIONABLE = 1.0e-4;   // Pc threshold declaring a conjunction
constexpr double DEFAULT_HBR_M = 10.0;     // per-craft hard-body radius default
constexpr int    CHAN_TERMS    = 24;       // fixed order of the Pc series (§4.5)
constexpr double VAR_FLOOR_M2  = 1.0;      // covariance eigenvalue floor (1 m^2)
constexpr double DEFAULT_VAR_M2 = 1.0e6;   // (1 km)^2: conservative default when
                                           // a craft publishes no covariance

// --- §5 Rule predicates ------------------------------------------------------
constexpr double OVERTAKE_COS   = 0.3826834323650898; // cos(67.5 deg), pinned
constexpr double EPS_SPEED_MPS  = 0.0625;  // 2^-4 m/s speed-ordering dead-band
constexpr double EPS_LATERAL_M  = 1.0;     // 2^0 m starboard-side dead-band

// --- §6 Maneuver -------------------------------------------------------------
constexpr double MANEUVER_MARGIN = 1.25;   // kappa: burn targets kappa*R_SAFE
constexpr double MIN_LEAD_S      = 1.0;    // burn lead floor (unbounded-dv guard)
constexpr int    RETURN_PHASING_ORBITS = 2; // N in the mirror+phase return

// --- §7 Multi-body -----------------------------------------------------------
constexpr uint32_t MAX_WINDOW_CRAFT = 32u;
constexpr uint32_t MAX_WINDOW_CONJ  = 64u;

// --- §11 / PROTOCOL: identity of the ruleset itself --------------------------
// Folded into every conjunction id (rules.hpp). A fork that alters the rules
// MUST change this value — and thereby hashes itself off-network.
constexpr uint32_t RULESET_ID = 0x53420001u;      // "SB", ruleset version 1
constexpr uint64_t TCA_BUCKET_MS = 16384u;        // 2^14 ms conj-id time bucket

// --- §3.4 Quantization --------------------------------------------------------
// Round-to-nearest, ties-to-even, onto the lattice with quantum q. Because q
// is a power of two, x/q and the product are exact scalings; the only rounding
// is the nearbyint itself (IEEE default mode), so this is deterministic.
inline double quantize(double x, double q) {
    return std::nearbyint(x / q) * q;
}

// --- §4.6 canon_exp: THE pinned transcendental --------------------------------
// exp(x) for x <= 0, defined operation-by-operation so every conformant
// implementation computes identical bits. Cody-Waite range reduction with the
// fdlibm split of ln2, then a 13-term Taylor polynomial evaluated in pinned
// Horner order, then an exact scale by 2^k. |r| <= ln2/2 gives a truncation
// error below 1 ulp for this use (probabilities compared against 1e-4).
// libm's exp() is NOT correctly rounded and MUST NOT be substituted on the
// verdict path.
inline double canon_exp(double x) {
    if (x < -708.0) { return 0.0; }
    if (x == 0.0)   { return 1.0; }
    constexpr double INV_LN2 = 1.4426950408889634;      // pinned literal
    constexpr double LN2_HI  = 6.93147180369123816490e-01; // fdlibm hi split
    constexpr double LN2_LO  = 1.90821492927058770002e-10; // fdlibm lo split
    const double kd = std::nearbyint(x * INV_LN2);
    const double r  = (x - kd * LN2_HI) - kd * LN2_LO;
    // Taylor coefficients 1/n!, n = 13 .. 2, pinned literals, pinned order.
    double p = 1.6059043836821613e-10;                  // 1/13!
    p = p * r + 2.08767569878681e-09;                   // 1/12!
    p = p * r + 2.505210838544172e-08;                  // 1/11!
    p = p * r + 2.7557319223985893e-07;                 // 1/10!
    p = p * r + 2.755731922398589e-06;                  // 1/9!
    p = p * r + 2.48015873015873e-05;                   // 1/8!
    p = p * r + 1.984126984126984e-04;                  // 1/7!
    p = p * r + 1.388888888888889e-03;                  // 1/6!
    p = p * r + 8.333333333333333e-03;                  // 1/5!
    p = p * r + 4.166666666666666e-02;                  // 1/4!
    p = p * r + 1.6666666666666666e-01;                 // 1/3!
    p = p * r + 0.5;                                    // 1/2!
    p = p * r + 1.0;                                    // 1/1!
    p = p * r + 1.0;                                    // 1/0!
    return std::ldexp(p, static_cast<int>(kd));
}

} // namespace canon
} // namespace starboard
