// SPDX-License-Identifier: AGPL-3.0-or-later
// Bare-metal flight image: boot self-test + one CDM decision + telemetry.
#include "../starboard_fsw.h"
#include <stdio.h>

static const char CDM[] =
    "CCSDS_CDM_VERS = 1.0\nMESSAGE_ID = BM-1\nORIGINATOR = BAREMETAL\n"
    "TCA = 2026-11-05T21:07:44.500\nMISS_DISTANCE = 140.0 [m]\n"
    "OBJECT = OBJECT1\nOBJECT_DESIGNATOR = 1\n"
    "INTERNATIONAL_DESIGNATOR = 2026-001A\nMANEUVERABLE = YES\n"
    "REF_FRAME = GCRF\nX = 6928.137000 [km]\nY = 0.0 [km]\nZ = 0.0 [km]\n"
    "X_DOT = 0.0 [km/s]\nY_DOT = 7.585000000 [km/s]\nZ_DOT = 0.0 [km/s]\n"
    "CR_R = 4.0E+02\nCT_R = 0.0\nCT_T = 9.0E+04\nCN_R = 0.0\nCN_T = 0.0\n"
    "CN_N = 2.5E+03\n"
    "OBJECT = OBJECT2\nOBJECT_DESIGNATOR = 2\n"
    "INTERNATIONAL_DESIGNATOR = 1993-036BX\nMANEUVERABLE = N/A\n"
    "REF_FRAME = GCRF\nX = 6928.277000 [km]\nY = 0.0 [km]\nZ = 0.0 [km]\n"
    "X_DOT = 0.0 [km/s]\nY_DOT = -2.275500000 [km/s]\nZ_DOT = 7.235000000 [km/s]\n"
    "CR_R = 2.5E+03\nCT_R = 0.0\nCT_T = 2.5E+05\nCN_R = 0.0\nCN_T = 0.0\n"
    "CN_N = 2.5E+03\n";

static SbFsw g_ctx;

int main(void)
{
    SbFswConfig cfg = {0};
    uint8_t pkt[SB_FSW_TM_MAX_BYTES];
    uint32_t n;
    uint32_t slot = 0U;
    SbFswStatus st;
    cfg.self_id = sb_make_object_id("2026-001A");
    cfg.self_tier = TIER_OPERATIONAL;
    cfg.self_can_maneuver = true;
    cfg.operating_mode = SB_MODE_AUTONOMOUS;
    cfg.apid_base = 0x3A0U;
    cfg.burn_lead_ms = 2700000;
    cfg.cmd_latency_ms = 60000;
    st = sb_fsw_init(&g_ctx, &cfg);
    printf("selftest mask 0x%08lX mode %d init %d\n",
           (unsigned long)g_ctx.selftest_failed, (int)g_ctx.mode, (int)st);
    st = sb_fsw_ingest_cdm(&g_ctx, CDM, (uint32_t)(sizeof(CDM) - 1U),
                           1477940000000LL, &slot);
    printf("ingest %d role %d rule %d dv %.6f m/s miss->%.1f m\n", (int)st,
           (int)g_ctx.enc[slot].decision.role,
           (int)g_ctx.enc[slot].decision.rule,
           g_ctx.enc[slot].decision.plan.dv_mag,
           g_ctx.enc[slot].decision.miss_m +
               g_ctx.enc[slot].decision.plan.displacement_m);
    while ((n = sb_fsw_poll_tm(&g_ctx, pkt, sizeof(pkt), 1477940000000LL)) != 0U)
    {
        printf("TM apid 0x%03X len %lu\n",
               ((unsigned)(pkt[0] & 7U) << 8) | pkt[1], (unsigned long)n);
    }
    return (g_ctx.selftest_failed == 0U) ? 0 : 1;
}
