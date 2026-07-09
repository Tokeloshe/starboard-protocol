// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// rules.c — canonical identity, trust projection, conjunction id (FNV-1a) and
// the Canonical Pair Transform verdict.

#include "starboard.h"

/* --- id.hpp --- */
ObjectId sb_make_object_id(const char *s)
{
    ObjectId id;
    int i = 0;
    while ((i < 12) && (s[i] != '\0'))
    {
        char ch = s[i];
        if ((ch >= 'a') && (ch <= 'z'))
        {
            ch = (char)(ch - ('a' - 'A'));
        }
        id.c[i] = ch;
        ++i;
    }
    while (i < 12)
    {
        id.c[i] = '\0';
        ++i;
    }
    return id;
}

int sb_id_compare(const ObjectId *a, const ObjectId *b)
{
    int i;
    for (i = 0; i < 12; ++i)
    {
        const unsigned char x = (unsigned char)a->c[i];
        const unsigned char y = (unsigned char)b->c[i];
        if (x != y)
        {
            return (x < y) ? -1 : 1;
        }
    }
    return 0;
}

bool sb_id_equal(const ObjectId *a, const ObjectId *b)
{
    return sb_id_compare(a, b) == 0;
}

/* --- trust.hpp --- */
bool sb_effective_can_maneuver(TrustLevel trust, bool asserted)
{
    return (trust == TRUST_VERIFIED) ? asserted : false;
}

Tier sb_effective_tier(TrustLevel trust, Tier asserted)
{
    return (trust == TRUST_VERIFIED) ? asserted : TIER_OPERATIONAL;
}

/* --- conjunction id (FNV-1a-64) --- */
uint64_t sb_conjunction_id(const ObjectId *a, const ObjectId *b,
                           uint64_t tca_epoch_ms)
{
    const bool a_lo = (sb_id_compare(a, b) <= 0);
    const ObjectId *lo = a_lo ? a : b;
    const ObjectId *hi = a_lo ? b : a;
    const uint64_t prime = 1099511628211ull;
    uint64_t h = 14695981039346656037ull;
    uint64_t bucket;
    int i;

    for (i = 0; i < 12; ++i)
    {
        h ^= (uint64_t)(uint8_t)lo->c[i];
        h *= prime;
    }
    for (i = 0; i < 12; ++i)
    {
        h ^= (uint64_t)(uint8_t)hi->c[i];
        h *= prime;
    }
    bucket = tca_epoch_ms / SB_TCA_BUCKET_MS;
    for (i = 0; i < 8; ++i)
    {
        h ^= (uint64_t)(uint8_t)(bucket >> (8 * i));
        h *= prime;
    }
    for (i = 0; i < 4; ++i)
    {
        h ^= (uint64_t)(uint8_t)(SB_RULESET_ID >> (8 * i));
        h *= prime;
    }
    return h;
}

/* --- rule predicates --- */
static bool sb_overtakes(const CraftDescriptor *x, const CraftDescriptor *y)
{
    const double sx = sb_vnorm(x->state.velocity);
    const double sy = sb_vnorm(y->state.velocity);
    Vec3 dv;
    double dvn;
    if ((sx - sy) <= SB_EPS_SPEED_MPS)
    {
        return false;
    }
    if (sy <= SB_EPS_SPEED_MPS)
    {
        return false;
    }
    dv = sb_vsub(x->state.velocity, y->state.velocity);
    dvn = sb_vnorm(dv);
    if (dvn <= SB_EPS_SPEED_MPS)
    {
        return false;
    }
    return sb_vdot(dv, y->state.velocity) > ((SB_OVERTAKE_COS * dvn) * sy);
}

static bool sb_sees_on_starboard(const CraftDescriptor *x,
                                 const CraftDescriptor *y)
{
    const Vec3 starboard =
        sb_vcross(sb_vnormalized(x->state.velocity),
                  sb_vnormalized(x->state.position));
    const Vec3 dv = sb_vsub(y->state.velocity, x->state.velocity);
    const double dvn = sb_vnorm(dv);
    if (dvn <= SB_EPS_SPEED_MPS)
    {
        const Vec3 to_y = sb_vsub(y->state.position, x->state.position);
        return sb_vdot(to_y, starboard) > SB_EPS_LATERAL_M;
    }
    return -(sb_vdot(dv, starboard)) > (SB_EPS_BEARING * dvn);
}

/* --- evaluate --- */
GiveWayVerdict sb_evaluate(const CraftDescriptor *self,
                           const CraftDescriptor *other,
                           const Conjunction *conj, uint64_t tca_epoch_ms)
{
    const bool self_is_lo =
        (sb_id_compare(&self->object_id, &other->object_id) <= 0);
    const CraftDescriptor *lo = self_is_lo ? self : other;
    const CraftDescriptor *hi = self_is_lo ? other : self;
    const uint64_t cid =
        sb_conjunction_id(&lo->object_id, &hi->object_id, tca_epoch_ms);
    bool lo_gives_way;
    DecidingRule rule;
    const bool cm_lo = sb_effective_can_maneuver(lo->trust, lo->can_maneuver);
    const bool cm_hi = sb_effective_can_maneuver(hi->trust, hi->can_maneuver);
    const Tier tier_lo = sb_effective_tier(lo->trust, lo->tier);
    const Tier tier_hi = sb_effective_tier(hi->trust, hi->tier);
    GiveWayVerdict v;

    (void)conj;

    if (cm_lo != cm_hi)
    {
        lo_gives_way = cm_lo;
        rule = RULE_CAPABILITY;
    }
    else if (tier_lo != tier_hi)
    {
        lo_gives_way = ((uint8_t)tier_lo < (uint8_t)tier_hi);
        rule = RULE_TIER;
    }
    else
    {
        const bool lo_overtakes = sb_overtakes(lo, hi);
        const bool hi_overtakes = sb_overtakes(hi, lo);
        if (lo_overtakes != hi_overtakes)
        {
            lo_gives_way = lo_overtakes;
            rule = RULE_OVERTAKING;
        }
        else
        {
            const bool lo_sees = sb_sees_on_starboard(lo, hi);
            const bool hi_sees = sb_sees_on_starboard(hi, lo);
            if (lo_sees != hi_sees)
            {
                lo_gives_way = lo_sees;
                rule = RULE_STARBOARD;
            }
            else
            {
                lo_gives_way = ((cid & 1ull) == 0ull);
                rule = RULE_FAIR_COIN;
            }
        }
    }

    v.deciding_rule = rule;
    v.conjunction_id = cid;
    v.give_way_id = lo_gives_way ? lo->object_id : hi->object_id;
    v.advisory = (!cm_lo) && (!cm_hi);
    v.self_role = sb_id_equal(&v.give_way_id, &self->object_id)
                      ? ROLE_GIVE_WAY : ROLE_STAND_ON;
    return v;
}
