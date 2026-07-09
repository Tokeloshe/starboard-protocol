// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — Layer 7: multi-body conjunction windows (SPEC §7).
//
// When 3+ craft share a screening window, pairwise verdicts must not
// contradict each other (a craft told to stand on for one neighbour must not
// simultaneously be told to maneuver for another without everyone knowing).
// The resolver is a SINGLE deterministic pass:
//
//   1. Sort the window's conjunctions by (TCA bucket, conjunction id) —
//      a canonical order every participant derives identically.
//   2. Walk the list once. Earlier verdicts impose ROLE LOCKS:
//        * a STAND_ON lock makes a craft "unable to maneuver" in later
//          pairs (Rule 1 then hands the burn to its counterpart);
//        * a GIVE_WAY lock biases later pairs to reuse the already-burning
//          craft (one combined maneuver instead of two).
//   3. A pair whose give-way craft is stand-on-locked (both locked) is
//      flagged ESCALATED: the verdict stands, and the craft MUST re-verify
//      its earlier stand-on commitments after planning (SPEC §7.5).
//
// Theorem 4 (SPEC §7.6): the pass is total, terminates in exactly one
// iteration, and every participant computes the identical assignment —
// the ordering and all inputs are shared, and evaluate() is deterministic.
// Fuel fairness follows from Rule 5's per-encounter coin (SPEC §5.7).
//
// Allocation-free: fixed-capacity, caller-owned arrays. Insertion sort keeps
// the hot path free of libc callbacks and is optimal at window sizes.
// ============================================================================

#include "rules.hpp"

namespace starboard {

// One detected pairwise conjunction inside a window. `a` and `b` index the
// window's craft table; `conj` is expressed with `a` as self.
struct WindowEntry {
    uint32_t    a, b;
    Conjunction conj;
    uint64_t    tca_epoch_ms;
};

struct WindowVerdict {
    uint64_t     conjunction_id;
    uint32_t     give_way_index;   // index into the craft table
    uint32_t     stand_on_index;
    DecidingRule deciding_rule;    // CASCADE if a lock decided the pair
    bool         escalated;        // give-way craft was stand-on-locked
    bool         advisory;
};

namespace detail {

enum class Lock : uint8_t { NONE = 0, STAND_ON = 1, GIVE_WAY = 2 };

inline bool window_entry_before(const WindowEntry& x, const WindowEntry& y,
                                const CraftDescriptor* craft) {
    const uint64_t bx = x.tca_epoch_ms / canon::TCA_BUCKET_MS;
    const uint64_t by = y.tca_epoch_ms / canon::TCA_BUCKET_MS;
    if (bx != by) { return bx < by; }
    const uint64_t cx = conjunction_id(craft[x.a].object_id,
                                       craft[x.b].object_id, x.tca_epoch_ms);
    const uint64_t cy = conjunction_id(craft[y.a].object_id,
                                       craft[y.b].object_id, y.tca_epoch_ms);
    return cx < cy;
}

} // namespace detail

// Resolve a window. `entries` is sorted in place into the canonical order;
// verdicts are written to `out` (same length as entries). Returns the number
// of verdicts, or 0 if capacities are exceeded (caller must split windows).
inline uint32_t resolve_window(const CraftDescriptor* craft, uint32_t n_craft,
                               WindowEntry* entries, uint32_t n_entries,
                               WindowVerdict* out) {
    if ((n_craft > canon::MAX_WINDOW_CRAFT) ||
        (n_entries > canon::MAX_WINDOW_CONJ)) {
        return 0u;
    }

    // Canonical order: insertion sort by (TCA bucket, conjunction id).
    for (uint32_t i = 1u; i < n_entries; ++i) {
        const WindowEntry key = entries[i];
        uint32_t j = i;
        while ((j > 0u) &&
               detail::window_entry_before(key, entries[j - 1u], craft)) {
            entries[j] = entries[j - 1u];
            --j;
        }
        entries[j] = key;
    }

    detail::Lock locks[canon::MAX_WINDOW_CRAFT] = {};

    for (uint32_t i = 0u; i < n_entries; ++i) {
        const WindowEntry& e = entries[i];
        CraftDescriptor a = craft[e.a];
        CraftDescriptor b = craft[e.b];

        WindowVerdict wv;
        const detail::Lock la = locks[e.a];
        const detail::Lock lb = locks[e.b];

        if ((la == detail::Lock::GIVE_WAY) != (lb == detail::Lock::GIVE_WAY)) {
            // Cascade: exactly one is already maneuvering — it absorbs this
            // pair too (one combined burn beats two independent ones).
            const bool a_gives = (la == detail::Lock::GIVE_WAY);
            wv.give_way_index = a_gives ? e.a : e.b;
            wv.stand_on_index = a_gives ? e.b : e.a;
            wv.deciding_rule = DecidingRule::CASCADE;
            wv.advisory = false;
            wv.conjunction_id = conjunction_id(a.object_id, b.object_id,
                                               e.tca_epoch_ms);
        } else {
            // Stand-on locks act through Rule 1: a locked craft is committed
            // to a predictable trajectory, which IS "cannot maneuver".
            if (la == detail::Lock::STAND_ON) { a.can_maneuver = false; }
            if (lb == detail::Lock::STAND_ON) { b.can_maneuver = false; }

            const GiveWayVerdict v = evaluate(a, b, e.conj, e.tca_epoch_ms);
            const bool a_gives = id_equal(v.give_way_id, a.object_id);
            wv.give_way_index = a_gives ? e.a : e.b;
            wv.stand_on_index = a_gives ? e.b : e.a;
            wv.deciding_rule = v.deciding_rule;
            wv.advisory = v.advisory;
            wv.conjunction_id = v.conjunction_id;
        }

        wv.escalated =
            (locks[wv.give_way_index] == detail::Lock::STAND_ON);

        // Record locks (first-assigned role is sticky for the window).
        if (locks[wv.give_way_index] == detail::Lock::NONE) {
            locks[wv.give_way_index] = detail::Lock::GIVE_WAY;
        }
        if (locks[wv.stand_on_index] == detail::Lock::NONE) {
            locks[wv.stand_on_index] = detail::Lock::STAND_ON;
        }

        out[i] = wv;
    }
    return n_entries;
}

} // namespace starboard
