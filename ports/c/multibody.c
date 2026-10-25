// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// multibody.c — Layer 7 single-pass window cascade (SPEC §7). Transliteration
// of multibody.hpp: canonical (TCA bucket, conjunction id) insertion sort,
// sticky role locks, advisory verdicts record no locks, malformed windows
// rejected before anything is indexed. Fixed capacity, no allocation.

#include "starboard.h"

#define SB_LOCK_NONE     (0U)
#define SB_LOCK_STAND_ON (1U)
#define SB_LOCK_GIVE_WAY (2U)

uint32_t sb_resolve_window(const CraftDescriptor *craft, uint32_t n_craft,
                           WindowEntry *entries, uint32_t n_entries,
                           WindowVerdict *out)
{
    uint64_t cid[SB_MAX_WINDOW_CONJ];
    uint8_t locks[SB_MAX_WINDOW_CRAFT];
    uint32_t i;

    if ((n_craft > SB_MAX_WINDOW_CRAFT) || (n_entries > SB_MAX_WINDOW_CONJ))
    {
        return 0U;
    }
    for (i = 0U; i < n_entries; ++i)
    {
        if ((entries[i].a >= n_craft) || (entries[i].b >= n_craft) ||
            (entries[i].a == entries[i].b))
        {
            return 0U;
        }
    }

    for (i = 0U; i < n_entries; ++i)
    {
        cid[i] = sb_conjunction_id(&craft[entries[i].a].object_id,
                                   &craft[entries[i].b].object_id,
                                   entries[i].tca_epoch_ms);
    }
    for (i = 1U; i < n_entries; ++i)
    {
        const WindowEntry key = entries[i];
        const uint64_t key_cid = cid[i];
        const uint64_t key_bucket = key.tca_epoch_ms / SB_TCA_BUCKET_MS;
        uint32_t j = i;
        while (j > 0U)
        {
            const uint64_t pb = entries[j - 1U].tca_epoch_ms / SB_TCA_BUCKET_MS;
            const bool before = (key_bucket != pb) ? (key_bucket < pb)
                                                   : (key_cid < cid[j - 1U]);
            if (!before)
            {
                break;
            }
            entries[j] = entries[j - 1U];
            cid[j] = cid[j - 1U];
            --j;
        }
        entries[j] = key;
        cid[j] = key_cid;
    }

    for (i = 0U; i < SB_MAX_WINDOW_CRAFT; ++i)
    {
        locks[i] = SB_LOCK_NONE;
    }

    for (i = 0U; i < n_entries; ++i)
    {
        const WindowEntry *e = &entries[i];
        CraftDescriptor a = craft[e->a];
        CraftDescriptor b = craft[e->b];
        WindowVerdict wv;
        const uint8_t la = locks[e->a];
        const uint8_t lb = locks[e->b];

        a.state = e->a_tca;
        b.state = e->b_tca;
        wv.conjunction_id = cid[i];

        if ((la == SB_LOCK_GIVE_WAY) != (lb == SB_LOCK_GIVE_WAY))
        {
            /* Cascade: the already-maneuvering craft absorbs this pair. */
            const bool a_gives = (la == SB_LOCK_GIVE_WAY);
            wv.give_way_index = a_gives ? e->a : e->b;
            wv.stand_on_index = a_gives ? e->b : e->a;
            wv.deciding_rule = RULE_CASCADE;
            wv.advisory = false;
        }
        else
        {
            GiveWayVerdict v;
            bool a_gives;
            /* A stand-on lock is a committed trajectory: Rule 1 incapable. */
            if (la == SB_LOCK_STAND_ON)
            {
                a.can_maneuver = false;
            }
            if (lb == SB_LOCK_STAND_ON)
            {
                b.can_maneuver = false;
            }
            v = sb_evaluate(&a, &b, e->tca_epoch_ms);
            a_gives = sb_id_equal(&v.give_way_id, &a.object_id);
            wv.give_way_index = a_gives ? e->a : e->b;
            wv.stand_on_index = a_gives ? e->b : e->a;
            wv.deciding_rule = v.deciding_rule;
            wv.advisory = v.advisory;
        }

        wv.escalated = (locks[wv.give_way_index] == SB_LOCK_STAND_ON);

        /* Advisory verdicts commit nobody (SPEC §7.3). */
        if (!wv.advisory)
        {
            if (locks[wv.give_way_index] == SB_LOCK_NONE)
            {
                locks[wv.give_way_index] = SB_LOCK_GIVE_WAY;
            }
            if (locks[wv.stand_on_index] == SB_LOCK_NONE)
            {
                locks[wv.stand_on_index] = SB_LOCK_STAND_ON;
            }
        }
        out[i] = wv;
    }
    return n_entries;
}
