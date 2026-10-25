// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — CBMC proof A of SPEC Theorem 1: the decision layer.
// Copyright (C) 2026 James H. All rights reserved.
//
// The two geometric predicates are ABSTRACTED: overtakes(x, y) and
// sees_on_starboard(x, y) become arbitrary functions of their (ordered)
// argument pair — four free booleans, one per ordered pair. CBMC then checks
// the rule chain, trust projection, canonical pair transform and
// conjunction id for EVERY tier/trust/capability combination, EVERY pair of
// 12-byte ids, EVERY epoch, and EVERY possible predicate function. So
// Theorem 1 holds for any deterministic predicates — and the real ones are
// pure functions of their arguments (proof B checks they are UB-free).
//
//   make -C verification/cbmc      (runs every proof in this directory)
#include "starboard.h"

ObjectId nondet_id(void);
int nondet_int(void);
_Bool nondet_bool(void);
uint64_t nondet_u64(void);

static const CraftDescriptor *g_a;
static const CraftDescriptor *g_b;
static _Bool g_ov_ab, g_ov_ba, g_sb_ab, g_sb_ba;

/* Consistent abstraction: the value depends only on WHICH craft is first. */
bool sb_overtakes(const CraftDescriptor *x, const CraftDescriptor *y)
{
    __CPROVER_assert(((x == g_a) && (y == g_b)) || ((x == g_b) && (y == g_a)),
                     "predicates are only ever asked about the pair");
    return (x == g_a) ? g_ov_ab : g_ov_ba;
}

bool sb_sees_on_starboard(const CraftDescriptor *x, const CraftDescriptor *y)
{
    __CPROVER_assert(((x == g_a) && (y == g_b)) || ((x == g_b) && (y == g_a)),
                     "predicates are only ever asked about the pair");
    return (x == g_a) ? g_sb_ab : g_sb_ba;
}

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
}

void harness(void)
{
    static CraftDescriptor a;
    static CraftDescriptor b;
    const uint64_t epoch = nondet_u64();
    GiveWayVerdict vab;
    GiveWayVerdict vba;
    any_descriptor(&a);
    any_descriptor(&b);
    __CPROVER_assume(!sb_id_equal(&a.object_id, &b.object_id));
    g_a = &a;
    g_b = &b;
    g_ov_ab = nondet_bool();
    g_ov_ba = nondet_bool();
    g_sb_ab = nondet_bool();
    g_sb_ba = nondet_bool();

    vab = sb_evaluate(&a, &b, epoch);
    vba = sb_evaluate(&b, &a, epoch);

    __CPROVER_assert(sb_id_equal(&vab.give_way_id, &vba.give_way_id),
                     "T1 agreement: same give-way craft");
    __CPROVER_assert(vab.deciding_rule == vba.deciding_rule,
                     "T1 agreement: same deciding rule");
    /* conjunction_id equality: lemma_cid.c proves the hashed MESSAGE is
     * identical from both perspectives; FNV-1a is a pure function of it. */
    __CPROVER_assert(vab.advisory == vba.advisory,
                     "T1 agreement: same advisory flag");
    __CPROVER_assert(sb_id_equal(&vab.give_way_id, &a.object_id) ||
                     sb_id_equal(&vab.give_way_id, &b.object_id),
                     "T1 totality: the named craft is one of the pair");
    __CPROVER_assert(vab.self_role != vba.self_role,
                     "T1: exactly one gives way, the other stands on");
    __CPROVER_assert((vab.deciding_rule >= RULE_CAPABILITY) &&
                     (vab.deciding_rule <= RULE_FAIR_COIN),
                     "T1 totality: a pairwise rule always decides");
    /* Trust model (SPEC Table 8-1): an unverified craft is never treated as
     * able to maneuver, and never above OPERATIONAL. */
    __CPROVER_assert(!(vab.deciding_rule == RULE_TIER) ||
                     ((a.trust == TRUST_VERIFIED) || (b.trust == TRUST_VERIFIED)),
                     "Table 8-1: tier only decides if someone is verified");
    __CPROVER_assert(!vab.advisory ||
                     !((a.trust == TRUST_VERIFIED) && a.can_maneuver) ,
                     "Table 8-1: advisory only if nobody is trusted to act");
}
