// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — CBMC proof B: the REAL rule predicates.
// Copyright (C) 2026 James H. All rights reserved.
//
// With the actual overtaking / starboard predicates (no abstraction), for
// EVERY bit pattern of every double in both descriptors — NaN, infinities,
// subnormals, -0 included — sb_evaluate has no out-of-bounds access, no
// invalid pointer use, no signed overflow, no invalid shift, and returns a
// well-formed verdict naming one of the two craft. The predicates read only
// their two arguments (no globals, no state), so they are pure functions —
// the premise proof A (theorem1_logic.c) quantifies over.
#include "starboard.h"

ObjectId nondet_id(void);
double nondet_double(void);
int nondet_int(void);
_Bool nondet_bool(void);
uint64_t nondet_u64(void);

static void any_descriptor(CraftDescriptor *d)
{
    const int tier = nondet_int();
    const int trust = nondet_int();
    __CPROVER_assume((tier >= 0) && (tier <= 3));
    __CPROVER_assume((trust >= 0) && (trust <= 2));
    d->object_id = nondet_id();
    d->tier = (Tier)tier;
    d->trust = (TrustLevel)trust;
    d->can_maneuver = nondet_bool();
    d->state.position.x = nondet_double();
    d->state.position.y = nondet_double();
    d->state.position.z = nondet_double();
    d->state.velocity.x = nondet_double();
    d->state.velocity.y = nondet_double();
    d->state.velocity.z = nondet_double();
}

void harness(void)
{
    CraftDescriptor a;
    CraftDescriptor b;
    GiveWayVerdict v;
    any_descriptor(&a);
    any_descriptor(&b);
    __CPROVER_assume(!sb_id_equal(&a.object_id, &b.object_id));
    v = sb_evaluate(&a, &b, nondet_u64());
    __CPROVER_assert(sb_id_equal(&v.give_way_id, &a.object_id) ||
                     sb_id_equal(&v.give_way_id, &b.object_id),
                     "verdict names one of the pair, for any doubles");
    __CPROVER_assert((v.deciding_rule >= RULE_CAPABILITY) &&
                     (v.deciding_rule <= RULE_FAIR_COIN),
                     "a rule always decides, for any doubles (incl. NaN)");
}
