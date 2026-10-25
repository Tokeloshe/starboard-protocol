// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — CBMC proof: Layer-4 notice decoding + flock ledger.
// Copyright (C) 2026 James H. All rights reserved.
//
// For EVERY 204-byte frame arriving over an inter-satellite link, decoding
// and folding it into a (bounded) flock ledger performs no out-of-bounds
// access, no invalid pointer use, no signed overflow, and a relay is either
// nothing or exactly one notice.
#include "starboard_flock.h"

uint8_t nondet_u8(void);

void harness(void)
{
    static SbFlock f;
    uint8_t frame[SB_NOTICE_BYTES];
    uint8_t relay[SB_NOTICE_BYTES];
    uint32_t rl = 7U;
    uint32_t i;
    for (i = 0U; i < SB_NOTICE_BYTES; ++i)
    {
        frame[i] = nondet_u8();
    }
    sb_flock_init(&f, sb_make_object_id("2026-999Z"));
    (void)sb_flock_receive(&f, frame, SB_NOTICE_BYTES, relay, &rl);
    __CPROVER_assert((rl == 0U) || (rl == SB_NOTICE_BYTES),
                     "relay is nothing or exactly one notice");
    __CPROVER_assert((f.received + f.rejected + f.duplicates) == 1U,
                     "every frame is accounted for exactly once");
}
