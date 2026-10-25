// SPDX-License-Identifier: AGPL-3.0-or-later
// Fuzzer: the whole flight layer fed hostile uplink. Any byte string as a
// CDM must yield either a decision or a reported rejection — never a crash,
// an out-of-bounds access, undefined behaviour, or a hang.
#include "starboard_fsw.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static SbFsw ctx;
    static int init = 0;
    uint8_t pkt[SB_FSW_TM_MAX_BYTES];
    SbFswStatus st;
    if (!init) {
        SbFswConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.self_id = sb_make_object_id("2026-555A");
        cfg.self_can_maneuver = true;
        cfg.self_tier = TIER_OPERATIONAL;
        cfg.operating_mode = SB_MODE_AUTONOMOUS;
        cfg.burn_lead_ms = 2700000;
        cfg.cmd_latency_ms = 60000;
        if (sb_fsw_init(&ctx, &cfg) != SB_FSW_OK) { abort(); }
        init = 1;
    }
    if (size > 16384u) { return 0; }
    st = sb_fsw_ingest_cdm(&ctx, (const char *)data, (uint32_t)size,
                           1400000000000ll, NULL);
    if ((int)st < 0 || (int)st > 7) { abort(); }
    while (sb_fsw_poll_tm(&ctx, pkt, sizeof(pkt), 1400000000000ll) != 0u) { }
    sb_fsw_expire(&ctx, 1400000000000ll, 0);
    return 0;
}
