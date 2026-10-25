// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — CBMC lemma for Theorem 1: the conjunction id.
// Copyright (C) 2026 James H. All rights reserved.
//
// For EVERY pair of 12-byte ids (equal or not) and EVERY epoch, the 36-byte
// message hashed into the conjunction id is identical whichever craft
// computes it. With FNV-1a a pure function of the message, both craft
// derive the same conjunction id — and so the same fair coin (Rule 5).
// Also: the message embeds RULESET_ID, so a fork with a different ruleset
// provably names every encounter differently (SPEC §9.3).
#include "starboard.h"

ObjectId nondet_id(void);
uint64_t nondet_u64(void);

void harness(void)
{
    const ObjectId a = nondet_id();
    const ObjectId b = nondet_id();
    const uint64_t t = nondet_u64();
    uint8_t m1[SB_CONJ_ID_MSG_BYTES];
    uint8_t m2[SB_CONJ_ID_MSG_BYTES];
    uint32_t i;
    sb_conjunction_id_message(&a, &b, t, m1);
    sb_conjunction_id_message(&b, &a, t, m2);
    for (i = 0U; i < SB_CONJ_ID_MSG_BYTES; ++i)
    {
        __CPROVER_assert(m1[i] == m2[i],
                         "conjunction-id message identical from both sides");
    }
    __CPROVER_assert((m1[32] == 0x01U) && (m1[33] == 0x00U) &&
                     (m1[34] == 0x42U) && (m1[35] == 0x53U),
                     "RULESET_ID is folded into every conjunction id");
    /* The lo id is the bytewise-smaller one (SPEC §3.2 canonical order). */
    {
        ObjectId lo;
        ObjectId hi;
        for (i = 0U; i < 12U; ++i)
        {
            lo.c[i] = (char)m1[i];
            hi.c[i] = (char)m1[12U + i];
        }
        __CPROVER_assert(sb_id_compare(&lo, &hi) <= 0,
                         "message orders the pair canonically");
    }
}
