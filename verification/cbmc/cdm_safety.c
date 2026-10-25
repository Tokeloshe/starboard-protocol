// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — CBMC proof: CDM parser memory safety.
// Copyright (C) 2026 James H. All rights reserved.
//
// For EVERY byte string up to SB_PROOF_CDM_LEN bytes (default 40; raise it
// on a bigger machine), sb_cdm_parse performs no out-of-bounds read or
// write, no invalid pointer use and no signed overflow, and either accepts
// or returns a defined rejection — never anything else.
#include "starboard_cdm.h"

#ifndef SB_PROOF_CDM_LEN
#define SB_PROOF_CDM_LEN 40
#endif

char nondet_char(void);
uint32_t nondet_u32(void);

void harness(void)
{
    char text[SB_PROOF_CDM_LEN];
    const uint32_t len = nondet_u32();
    SbCdm out;
    uint32_t line = 0U;
    uint32_t i;
    SbCdmStatus st;
    __CPROVER_assume(len <= SB_PROOF_CDM_LEN);
    for (i = 0U; i < SB_PROOF_CDM_LEN; ++i)
    {
        text[i] = nondet_char();
    }
    st = sb_cdm_parse(text, len, &out, &line);
    __CPROVER_assert(((int)st >= 0) && ((int)st <= 10), "defined status");
    __CPROVER_assert(line <= (len + 1U), "error line within the input");
}
