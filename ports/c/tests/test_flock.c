// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — Layer 4 (flock) tests.
// Copyright (C) 2026 James H. All rights reserved.

#include "../starboard_flock.h"
#include "../starboard_fsw.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(name, cond)                                                     \
    do {                                                                      \
        if (cond) { ++g_pass; }                                               \
        else { ++g_fail; printf("  FAIL  %s  (line %d)\n", name, __LINE__); } \
    } while (0)

static SbNotice sample(const char *origin, uint32_t seq, uint64_t conj)
{
    SbNotice n;
    (void)memset(&n, 0, sizeof(n));
    n.hops = 3U;
    n.origin = sb_make_object_id(origin);
    n.seq = seq;
    n.conjunction_id = conj;
    n.counterpart = sb_make_object_id("1993-036BX");
    n.tca_epoch_ms = 1477000000000LL;
    n.burn_epoch_ms = n.tca_epoch_ms - 2700000;
    n.post_burn.position.x = 6.9e6;
    n.post_burn.velocity.y = 7600.0;
    n.dv_eci.y = 0.3;
    n.mirror_epoch_ms = n.burn_epoch_ms + 5700000;
    n.mirror_dv_hill.y = -0.15;
    n.final_epoch_ms = n.mirror_epoch_ms + 11400000;
    n.final_dv_hill.y = 0.15;
    return n;
}

int main(void)
{
    uint8_t w[SB_NOTICE_BYTES];
    uint8_t relay[SB_NOTICE_BYTES];
    uint32_t rl = 0U;
    SbNotice n = sample("2026-300A", 1U, 0xABCDULL);
    SbNotice back;
    SbFlock f;
    SbFlock g;

    printf("Starboard flock (Layer 4) tests\n");
    CHECK("encode length", sb_notice_encode(&n, w) == SB_NOTICE_BYTES);
    CHECK("decode round-trip", sb_notice_decode(w, SB_NOTICE_BYTES, &back) &&
          (back.seq == 1U) && (back.hops == 3U) &&
          (back.dv_eci.y == 0.3) && (back.final_dv_hill.y == 0.15) &&
          sb_id_equal(&back.origin, &n.origin));
    {
        uint8_t bad[SB_NOTICE_BYTES];
        uint32_t i;
        int rejected = 0;
        for (i = 0U; i < SB_NOTICE_BYTES; ++i) {
            if (i == 6U) { continue; }          /* hop byte is not covered */
            (void)memcpy(bad, w, sizeof(bad));
            bad[i] ^= 0x04U;
            rejected += sb_notice_decode(bad, SB_NOTICE_BYTES, &back) ? 0 : 1;
        }
        CHECK("every single-bit corruption rejected (CRC/format)",
              rejected == (int)SB_NOTICE_BYTES - 1);
        CHECK("short frame rejected", !sb_notice_decode(w, 100U, &back));
    }
    {
        SbNotice nan = n;
        uint8_t wn[SB_NOTICE_BYTES];
        nan.post_burn.position.y = 0.0 / 0.0 * 0.0 + (1.0 / 0.0 - 1.0 / 0.0);
        (void)sb_notice_encode(&nan, wn);
        CHECK("non-finite state rejected", !sb_notice_decode(wn, SB_NOTICE_BYTES, &back));
    }

    sb_flock_init(&f, sb_make_object_id("2026-301A"));
    CHECK("new notice stored + relayed with hop-1",
          (sb_flock_receive(&f, w, SB_NOTICE_BYTES, relay, &rl) == SB_FLOCK_NEW) &&
          (rl == SB_NOTICE_BYTES) && (relay[6] == 2U));
    CHECK("duplicate dropped, not relayed",
          (sb_flock_receive(&f, w, SB_NOTICE_BYTES, relay, &rl) ==
           SB_FLOCK_DUPLICATE) && (rl == 0U));
    CHECK("relayed copy is a duplicate too (hops excluded from identity)",
          sb_flock_receive(&f, relay, SB_NOTICE_BYTES, relay, &rl) ==
          SB_FLOCK_DUPLICATE);
    {
        SbNotice n2 = sample("2026-300A", 2U, 0xABCDULL);
        uint8_t w2[SB_NOTICE_BYTES];
        n2.dv_eci.y = 0.4;
        (void)sb_notice_encode(&n2, w2);
        CHECK("newer seq updates in place",
              (sb_flock_receive(&f, w2, SB_NOTICE_BYTES, relay, &rl) ==
               SB_FLOCK_UPDATED) && (f.e[0].n.dv_eci.y == 0.4));
        CHECK("older seq afterwards is stale",
              sb_flock_receive(&f, w, SB_NOTICE_BYTES, relay, &rl) ==
              SB_FLOCK_DUPLICATE);
    }
    {
        SbNotice z = sample("2026-300A", 1U, 1U);
        uint8_t wz[SB_NOTICE_BYTES];
        z.hops = 0U;
        (void)sb_notice_encode(&z, wz);
        CHECK("hop limit 0: stored, not relayed",
              (sb_flock_receive(&f, wz, SB_NOTICE_BYTES, relay, &rl) ==
               SB_FLOCK_NEW) && (rl == 0U));
    }
    {
        SbNotice mine = sample("2026-301A", 1U, 7U);
        uint8_t wm[SB_NOTICE_BYTES];
        (void)sb_notice_encode(&mine, wm);
        CHECK("own notice echoed back is ignored",
              sb_flock_receive(&f, wm, SB_NOTICE_BYTES, relay, &rl) ==
              SB_FLOCK_REJECTED);
    }
    {
        /* Ledger digest is order-independent: same set, different arrival
         * order -> same digest. */
        SbNotice a = sample("2026-310A", 1U, 11U);
        SbNotice b = sample("2026-311A", 1U, 12U);
        uint8_t wa[SB_NOTICE_BYTES];
        uint8_t wb[SB_NOTICE_BYTES];
        SbFlock x;
        SbFlock y;
        (void)sb_notice_encode(&a, wa);
        (void)sb_notice_encode(&b, wb);
        sb_flock_init(&x, sb_make_object_id("2026-399A"));
        sb_flock_init(&y, sb_make_object_id("2026-398A"));
        (void)sb_flock_receive(&x, wa, SB_NOTICE_BYTES, relay, &rl);
        (void)sb_flock_receive(&x, wb, SB_NOTICE_BYTES, relay, &rl);
        (void)sb_flock_receive(&y, wb, SB_NOTICE_BYTES, relay, &rl);
        (void)sb_flock_receive(&y, wa, SB_NOTICE_BYTES, relay, &rl);
        CHECK("ledger digest independent of arrival order",
              sb_flock_digest(&x) == sb_flock_digest(&y));
    }
    {
        uint32_t i;
        int full = 0;
        sb_flock_init(&g, sb_make_object_id("2026-302A"));
        for (i = 0U; i < SB_FLOCK_MAX_ENTRIES + 4U; ++i) {
            SbNotice k = sample("2026-300A", 1U, 1000U + i);
            uint8_t wk[SB_NOTICE_BYTES];
            (void)sb_notice_encode(&k, wk);
            full += (sb_flock_receive(&g, wk, SB_NOTICE_BYTES, relay, &rl) ==
                     SB_FLOCK_FULL) ? 1 : 0;
        }
        CHECK("bounded ledger reports FULL, never overruns", full == 4);
        sb_flock_expire(&g, g.e[0].n.final_epoch_ms + 10, 0);
        CHECK("expiry frees entries", !g.e[0].used);
    }
    {
        /* The check sees a mover passing right through own position. */
        SbNotice m = sample("2026-300A", 1U, 5U);
        CraftState own = m.post_burn;
        SbFlockCheck c;
        own.velocity.y = -7600.0;           /* head-on, same point */
        c = sb_flock_check(&m, own, m.burn_epoch_ms);
        CHECK("check flags a crossing at the burn point",
              c.conflict && (c.min_sep_m < 1.0));
        own.position.z = 50000.0;
        c = sb_flock_check(&m, own, m.burn_epoch_ms);
        CHECK("check clears a craft 50 km away", !c.conflict);
    }
    printf("%s: %d passed, %d failed\n", (g_fail == 0) ? "ALL PASS" : "FAILED",
           g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
