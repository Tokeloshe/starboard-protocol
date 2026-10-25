// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// broadphase.c — Stage-1 radial-band sweep (SPEC §4.8). Same gate as the C++
// reference (identical radial_bounds on the identical canonical state, margin
// R_SCREEN), so recall = 1.0 unconditionally. std::sort is replaced by an
// in-place, non-recursive heapsort keyed on (lo, original index): O(N log N),
// no allocation, no recursion, and — unlike std::sort — a TOTAL order, so the
// emitted pair list is itself deterministic across implementations. (SPEC
// §4.8 only requires the candidate SET to be a superset; the C++ reference
// may list the same set in another order.)

#include "starboard.h"

static bool sb_band_less(const SbBand *x, const SbBand *y)
{
    if (x->lo != y->lo)
    {
        return x->lo < y->lo;
    }
    return x->idx < y->idx;
}

static void sb_sift_down(SbBand *b, uint32_t start, uint32_t end)
{
    uint32_t root = start;
    for (;;)
    {
        uint32_t child = (2U * root) + 1U;
        uint32_t swap = root;
        SbBand t;
        if (child > end)
        {
            break;
        }
        if (sb_band_less(&b[swap], &b[child]))
        {
            swap = child;
        }
        if (((child + 1U) <= end) && sb_band_less(&b[swap], &b[child + 1U]))
        {
            swap = child + 1U;
        }
        if (swap == root)
        {
            break;
        }
        t = b[root];
        b[root] = b[swap];
        b[swap] = t;
        root = swap;
    }
}

static void sb_heapsort_bands(SbBand *b, uint32_t n)
{
    uint32_t end;
    uint32_t start;
    if (n < 2U)
    {
        return;
    }
    start = (n - 2U) / 2U;
    for (;;)
    {
        sb_sift_down(b, start, n - 1U);
        if (start == 0U)
        {
            break;
        }
        --start;
    }
    end = n - 1U;
    while (end > 0U)
    {
        const SbBand t = b[end];
        b[end] = b[0];
        b[0] = t;
        --end;
        sb_sift_down(b, 0U, end);
    }
}

uint32_t sb_radial_band_pairs(const CraftState *objs, uint32_t n,
                              SbBand *bands, uint32_t *active,
                              SbPair *out, uint32_t cap, bool *overflow,
                              double margin)
{
    uint32_t k;
    uint32_t j;
    uint32_t count = 0U;
    uint32_t n_active = 0U;

    if (overflow != NULL)
    {
        *overflow = false;
    }
    for (k = 0U; k < n; ++k)
    {
        double rlo;
        double rhi;
        sb_radial_bounds(sb_canonicalize(objs[k]), &rlo, &rhi);
        bands[k].lo = rlo - margin;
        bands[k].hi = rhi + margin;
        bands[k].idx = k;
    }
    sb_heapsort_bands(bands, n);

    for (j = 0U; j < n; ++j)
    {
        const double lo_j = bands[j].lo;
        const uint32_t ib = bands[j].idx;
        uint32_t w = 0U;
        uint32_t s;
        for (s = 0U; s < n_active; ++s)
        {
            const uint32_t a = active[s];
            uint32_t ia;
            if (bands[a].hi < lo_j)
            {
                continue;
            }
            active[w] = a;
            ++w;
            ia = bands[a].idx;
            if (count >= cap)
            {
                if (overflow != NULL)
                {
                    *overflow = true;
                }
                return count;
            }
            out[count].i = (ia < ib) ? ia : ib;
            out[count].j = (ia < ib) ? ib : ia;
            ++count;
        }
        n_active = w;
        active[n_active] = j;
        ++n_active;
    }
    return count;
}
