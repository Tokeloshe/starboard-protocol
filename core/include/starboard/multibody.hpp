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
// window's craft table. The TCA states (from screening) ride along because
// rule geometry is evaluated at the encounter, and each pair in the window
// has its own TCA.
struct WindowEntry {
    uint32_t    a, b;
    uint64_t    tca_epoch_ms;
    CraftState  a_tca;
    CraftState  b_tca;
};

struct WindowVerdict {
    uint64_t     conjunction_id;
    uint32_t     give_way_index;   // index into the craft table
    uint32_t     stand_on_index;
    DecidingRule deciding_rule;    // CASCADE if a lock decided the pair
    bool         escalated;        // give-way craft was stand-on-locked
    bool         advisory;         // nobody trusted to maneuver: alert-only
};

namespace detail {

enum class Lock : uint8_t { NONE = 0, STAND_ON = 1, GIVE_WAY = 2 };

} // namespace detail

// Resolve a window. `entries` is sorted in place into the canonical order;
// verdicts are written to `out` (same length as entries). Returns the number
// of verdicts, or 0 if a capacity is exceeded (caller must split windows) or
// any entry's craft indices are out of range / self-paired (malformed input
// is rejected, never indexed).
inline uint32_t resolve_window(const CraftDescriptor* craft, uint32_t n_craft,
                               WindowEntry* entries, uint32_t n_entries,
                               WindowVerdict* out) {
    if ((n_craft > canon::MAX_WINDOW_CRAFT) ||
        (n_entries > canon::MAX_WINDOW_CONJ)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < n_entries; ++i) {
        if ((entries[i].a >= n_craft) || (entries[i].b >= n_craft) ||
            (entries[i].a == entries[i].b)) {
            return 0u;   // would index craft[]/locks[] out of bounds
        }
    }

    // Canonical sort key: (TCA bucket, conjunction id). Ids are precomputed
    // once per entry — recomputing the FNV hash inside every insertion-sort
    // comparison would cost O(n^2) redundant hashes.
    uint64_t cid[canon::MAX_WINDOW_CONJ];
    for (uint32_t i = 0u; i < n_entries; ++i) {
        cid[i] = conjunction_id(craft[entries[i].a].object_id,
                                craft[entries[i].b].object_id,
                                entries[i].tca_epoch_ms);
    }
    for (uint32_t i = 1u; i < n_entries; ++i) {
        const WindowEntry key = entries[i];
        const uint64_t key_cid = cid[i];
        const uint64_t key_bucket = key.tca_epoch_ms / canon::TCA_BUCKET_MS;
        uint32_t j = i;
        while (j > 0u) {
            const uint64_t pb =
                entries[j - 1u].tca_epoch_ms / canon::TCA_BUCKET_MS;
            const bool before =
                (key_bucket != pb) ? (key_bucket < pb) : (key_cid < cid[j - 1u]);
            if (!before) { break; }
            entries[j] = entries[j - 1u];
            cid[j] = cid[j - 1u];
            --j;
        }
        entries[j] = key;
        cid[j] = key_cid;
    }

    detail::Lock locks[canon::MAX_WINDOW_CRAFT] = {};

    for (uint32_t i = 0u; i < n_entries; ++i) {
        const WindowEntry& e = entries[i];
        CraftDescriptor a = craft[e.a];
        CraftDescriptor b = craft[e.b];
        a.state = e.a_tca;
        b.state = e.b_tca;

        WindowVerdict wv;
        wv.conjunction_id = cid[i];
        const detail::Lock la = locks[e.a];
        const detail::Lock lb = locks[e.b];

        if ((la == detail::Lock::GIVE_WAY) != (lb == detail::Lock::GIVE_WAY)) {
            // Cascade: exactly one is already maneuvering — it absorbs this
            // pair too (one combined burn beats two independent ones).
            // Advisory locks never reach here (see below), so the cascading
            // craft is genuinely maneuverable and the pair is not advisory.
            const bool a_gives = (la == detail::Lock::GIVE_WAY);
            wv.give_way_index = a_gives ? e.a : e.b;
            wv.stand_on_index = a_gives ? e.b : e.a;
            wv.deciding_rule = DecidingRule::CASCADE;
            wv.advisory = false;
        } else {
            // Stand-on locks act through Rule 1: a locked craft is committed
            // to a predictable trajectory, which IS "cannot maneuver".
            if (la == detail::Lock::STAND_ON) { a.can_maneuver = false; }
            if (lb == detail::Lock::STAND_ON) { b.can_maneuver = false; }

            const GiveWayVerdict v = evaluate(a, b, e.tca_epoch_ms);
            const bool a_gives = id_equal(v.give_way_id, a.object_id);
            wv.give_way_index = a_gives ? e.a : e.b;
            wv.stand_on_index = a_gives ? e.b : e.a;
            wv.deciding_rule = v.deciding_rule;
            wv.advisory = v.advisory;
        }

        wv.escalated =
            (locks[wv.give_way_index] == detail::Lock::STAND_ON);

        // Record locks (first-assigned role is sticky for the window) — but
        // NEVER from an advisory verdict: an advisory names a give-way craft
        // for alerting only, nobody is trusted to act, so it commits no one.
        // Without this, an unverifiable derelict could acquire a GIVE_WAY
        // lock and later "absorb" a real conjunction via the cascade branch,
        // telling a healthy counterpart to stand on while a corpse is
        // supposedly maneuvering (SPEC §7.3).
        if (!wv.advisory) {
            if (locks[wv.give_way_index] == detail::Lock::NONE) {
                locks[wv.give_way_index] = detail::Lock::GIVE_WAY;
            }
            if (locks[wv.stand_on_index] == detail::Lock::NONE) {
                locks[wv.stand_on_index] = detail::Lock::STAND_ON;
            }
        }

        out[i] = wv;
    }
    return n_entries;
}

} // namespace starboard
