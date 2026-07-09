// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — Layer 0 broad-phase (SPEC §4.8).
//
// At constellation scale (10^4–10^5 objects) the O(N^2) all-pairs screen is
// infeasible. The broad-phase cheaply produces a CANDIDATE PAIR LIST that is
// a strict SUPERSET of every pair screen_orbits would declare a conjunction,
// so the expensive per-pair search runs only on candidates.
//
// STAGE 1 — RADIAL-BAND SWEEP (this header; unconditionally complete).
// Each object occupies a radial shell [periapsis − M, apoapsis + M], with the
// periapsis/apoapsis from the SAME detail::radial_bounds (vis-viva, algebraic)
// that screen_orbits uses for its own ring prefilter. Two objects are emitted
// as a candidate iff their padded shells overlap.
//
// RECALL = 1.0, unconditional (proof, normative):
//   screen_orbits reports a conjunction ONLY after passing its ring gate
//   (screening.hpp): the separation `gap` of the two-body radial intervals
//   [rp, ra] satisfies gap ≤ R_SCREEN_M. Two bands each padded by M overlap
//   whenever the interval separation < 2M. With M = R_SCREEN_M, 2M > R_SCREEN_M,
//   so every pair screen_orbits keeps has overlapping bands and is emitted.
//   The bands are computed by the identical function on the identical
//   canonicalized state, so Stage 1's gate is a strict RELAXATION of
//   screen_orbits' own gate and can never drop a kept pair — regardless of
//   speeds, eccentricity, or geometry. Non-elliptic states get an unbounded
//   band (matching radial_bounds) and overlap everything, exactly as
//   screen_orbits' gate treats them.
//
// Completeness is per-craft (no missed neighbour of self); the broad-phase is
// NOT required to be bit-identical across implementations — it only selects
// which pairs to screen; the per-pair verdict stays deterministic (SPEC §4.8).
//
// Stage 1 is O(N log N) to DECIDE but emits Θ(N^2) candidates on a dense
// band (radial shells overlap heavily), pruning by a constant factor (~4×).
// The sub-quadratic Stage 2 (temporal spatial-hash) composes on top; its
// design and measured scaling are in SPEC §4.8 and conformance/validation.
//
// Allocation-free: the caller owns all buffers (bands[n], active[n], out[cap]).
// Uses std::sort on the caller's scratch (in-place, no heap); a MISRA-C port
// substitutes an explicit sort.
// ============================================================================

#include "screening.hpp"

#include <algorithm>

namespace starboard {
namespace broadphase {

// A candidate pair, as indices into the caller's object array (i < j).
struct Pair {
    uint32_t i;
    uint32_t j;
};

// Per-object radial band; also the sort scratch. Caller provides bands[n].
struct Band {
    double   lo;   // periapsis − margin
    double   hi;   // apoapsis + margin
    uint32_t idx;  // original object index
};

// Radial-band Stage 1. Fills `out` (capacity `cap`) with candidate pairs and
// returns the count. `bands` and `active` are caller scratch of length n.
// Sets *overflow if the output buffer fills (caller enlarges `out` or tiles
// the catalog). Default margin = R_SCREEN_M (2× cushion over the R_SCREEN_M/2
// minimum). Emits pairs (min,max) in original-index order.
inline uint32_t radial_band_pairs(const CraftState* objs, uint32_t n,
                                  Band* bands, uint32_t* active,
                                  Pair* out, uint32_t cap, bool* overflow,
                                  double margin = canon::R_SCREEN_M) {
    if (overflow != nullptr) { *overflow = false; }
    for (uint32_t k = 0u; k < n; ++k) {
        double rlo, rhi;
        detail::radial_bounds(canonicalize(objs[k]), &rlo, &rhi);
        bands[k].lo = rlo - margin;
        bands[k].hi = rhi + margin;
        bands[k].idx = k;
    }
    std::sort(bands, bands + n,
              [](const Band& x, const Band& y) { return x.lo < y.lo; });

    // Sweep in lo-order. `active` holds the sorted positions of bands still
    // overlapping the sweep front (hi ≥ current lo). For band j, all active
    // bands overlap it (lo_i ≤ lo_j and hi_i ≥ lo_j ⇒ intervals meet).
    uint32_t count = 0u;
    uint32_t n_active = 0u;
    for (uint32_t j = 0u; j < n; ++j) {
        const double lo_j = bands[j].lo;
        // Purge expired actives (compact in place).
        uint32_t w = 0u;
        for (uint32_t s = 0u; s < n_active; ++s) {
            if (bands[active[s]].hi >= lo_j) {
                active[w] = active[s];
                ++w;
            }
        }
        n_active = w;
        // Every surviving active overlaps band j ⇒ emit the pair.
        for (uint32_t s = 0u; s < n_active; ++s) {
            const uint32_t ia = bands[active[s]].idx;
            const uint32_t ib = bands[j].idx;
            if (count >= cap) {
                if (overflow != nullptr) { *overflow = true; }
                return count;
            }
            out[count].i = (ia < ib) ? ia : ib;
            out[count].j = (ia < ib) ? ib : ia;
            ++count;
        }
        active[n_active] = j;
        ++n_active;
    }
    return count;
}

} // namespace broadphase
} // namespace starboard
