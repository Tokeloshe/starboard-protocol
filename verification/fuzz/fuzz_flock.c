// SPDX-License-Identifier: AGPL-3.0-or-later
// Fuzzer: maneuver-notice decoding + flock ledger + flock check. Arbitrary
// bytes from an inter-satellite link must never crash, hang, or corrupt the
// ledger; accepted notices must re-encode to the same bytes (canonical).
#include "starboard_flock.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static SbFlock f;
    static int init = 0;
    uint8_t relay[SB_NOTICE_BYTES];
    uint32_t rl = 0u;
    SbNotice n;
    if (!init) { sb_flock_init(&f, sb_make_object_id("2026-999Z")); init = 1; }
    if (size > 4096u) { return 0; }
    if (sb_notice_decode(data, (uint32_t)size, &n)) {
        uint8_t re[SB_NOTICE_BYTES];
        SbFlockCheck c;
        (void)sb_notice_encode(&n, re);
        if (memcmp(re, data, SB_NOTICE_BYTES) != 0) { abort(); }
        c = sb_flock_check(&n, n.post_burn, n.burn_epoch_ms);
        if (!(c.min_sep_m >= 0.0)) { abort(); }
    }
    (void)sb_flock_receive(&f, data, (uint32_t)size, relay, &rl);
    if ((rl != 0u) && (rl != SB_NOTICE_BYTES)) { abort(); }
    return 0;
}
