/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Starboard Protocol — reference FLIGHT LOOP: where every call goes.
 *
 * Copy this pattern into your flight software. Everything marked
 * "YOUR PLATFORM" is a hook you replace with your own uplink, downlink,
 * inter-satellite link and attitude & orbit control (AOCS) calls; in
 * NASA cFS each hook is a Software Bus message, in F Prime a port call,
 * in an RTOS a queue. docs/INTEGRATION.md maps them per framework.
 *
 * Here three spacecraft run the loop in one process: SAT-A and SAT-B get
 * the same collision warning; SAT-C is a neighbour that only hears about
 * the maneuver over the inter-satellite link.
 *
 * Build:
 *   cc -std=c99 -O2 -ffp-contract=off -Idist examples/flight_loop.c
 *      dist/starboard.c -lm -o flight_loop
 *   ./flight_loop examples/cdm/crossing_demo.cdm
 */
#include "starboard.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    SbFsw       fsw;      /* the flight layer: ~30 KB, static, no heap      */
    SbFlock     flock;    /* Layer 4: what the neighbours are doing         */
} Spacecraft;

/* ---- YOUR PLATFORM: replace these hooks --------------------------------- */

static void downlink_send(const Spacecraft *sc, const uint8_t *pkt,
                          uint32_t len)
{
    /* CCSDS Space Packet, CRC-16 trailer: hand it to your telemetry path.
     * APID = base (housekeeping), base+1 (decision event), base+2 (burn). */
    const unsigned apid = ((unsigned)(pkt[0] & 0x07U) << 8) | pkt[1];
    printf("  [%s] downlink: %u-byte telemetry packet, APID 0x%03X\n",
           sc->name, len, apid);
}

static void aocs_schedule_burn(const Spacecraft *sc, const SbDecision *d)
{
    /* AUTONOMOUS mode only. Hand the burn to orbit control. */
    printf("  [%s] AOCS: burn %.4f m/s at GPS %lld ms, RTN [%+.4f %+.4f "
           "%+.4f] m/s; return %.4f m/s afterwards\n", sc->name,
           d->plan.dv_mag, (long long)d->burn_epoch_ms, d->plan.dv_hill.x,
           d->plan.dv_hill.y, d->plan.dv_hill.z, d->ret.total_dv);
}

static uint8_t isl_air[8][SB_NOTICE_BYTES];   /* a toy radio channel */
static uint32_t isl_count = 0U;

static void isl_broadcast(const Spacecraft *sc, const uint8_t *bytes)
{
    printf("  [%s] inter-satellite link: broadcast %u-byte maneuver notice\n",
           sc->name, SB_NOTICE_BYTES);
    if (isl_count < 8U)
    {
        memcpy(isl_air[isl_count++], bytes, SB_NOTICE_BYTES);
    }
}

/* ---- the loop ----------------------------------------------------------- */

/* 1. Boot: init runs the built-in self-test. A failure latches SAFE mode
 *    and the craft never commands a burn from Starboard. */
static int boot(Spacecraft *sc, const char *designator, SbMode mode)
{
    SbFswConfig cfg;
    sb_easy_profile_config(SB_SAT_CONSTELLATION, &cfg);
    cfg.self_id = sb_make_object_id(designator);
    cfg.operating_mode = mode;     /* start in ADVISORY ("shadow mode") */
    sb_fsw_init(&sc->fsw, &cfg);
    sb_flock_init(&sc->flock, cfg.self_id);
    printf("[%s] boot: self-test %s, mode %s\n", sc->name,
           sc->fsw.selftest_failed ? "FAILED" : "pass",
           sc->fsw.mode == SB_MODE_AUTONOMOUS ? "AUTONOMOUS"
         : sc->fsw.mode == SB_MODE_ADVISORY ? "ADVISORY" : "SAFE");
    return sc->fsw.selftest_failed == 0U;
}

/* 2. A collision warning arrives (uplinked, or computed on board). */
static void on_cdm(Spacecraft *sc, const char *cdm, uint32_t len,
                   int64_t now)
{
    uint32_t slot = 0U;
    uint8_t notice[SB_NOTICE_BYTES];
    const SbFswStatus st = sb_fsw_ingest_cdm(&sc->fsw, cdm, len, now, &slot);
    const SbDecision *d = &sc->fsw.enc[slot].decision;
    if (st == SB_FSW_ERR_NOT_INVOLVED)
    {
        return;                    /* not about us */
    }
    if (st != SB_FSW_OK)
    {
        printf("  [%s] warning rejected (status %d): report it, do not "
               "treat it as safe\n", sc->name, (int)st);
        return;
    }
    printf("  [%s] decided: %s\n", sc->name,
           d->role == SB_ROLE_GIVE_WAY ? "GIVE WAY"
         : d->role == SB_ROLE_STAND_ON ? "STAND ON" : "no action");
    if ((d->role == SB_ROLE_GIVE_WAY) && d->plan.required)
    {
        if (sc->fsw.mode == SB_MODE_AUTONOMOUS)
        {
            aocs_schedule_burn(sc, d);
        }
        /* Tell the flock (even in ADVISORY: neighbours plan around it). */
        if (sb_fsw_notice(&sc->fsw, slot, notice) == SB_NOTICE_BYTES)
        {
            isl_broadcast(sc, notice);
        }
    }
}

/* 3. A maneuver notice arrives from a neighbour. */
static void on_isl(Spacecraft *sc, const uint8_t *bytes, CraftState own,
                   int64_t own_epoch)
{
    uint8_t relay[SB_NOTICE_BYTES];
    uint32_t relay_len = 0U;
    SbNotice n;
    const SbFlockResult r = sb_flock_receive(&sc->flock, bytes,
                                             SB_NOTICE_BYTES, relay,
                                             &relay_len);
    if ((r != SB_FLOCK_NEW) && (r != SB_FLOCK_UPDATED))
    {
        return;                    /* duplicate, our own, or malformed */
    }
    (void)sb_notice_decode(bytes, SB_NOTICE_BYTES, &n);
    {
        const SbFlockCheck c = sb_flock_check(&n, own, own_epoch);
        printf("  [%s] heard a maneuver notice: closest pass with the mover "
               "%.0f m -> %s\n", sc->name, c.min_sep_m,
               c.conflict ? "CONFLICT: raise an alert / re-plan"
                          : "clear, no action");
    }
    if (relay_len != 0U)
    {
        printf("  [%s] relaying it onward (hop-limited)\n", sc->name);
    }
}

/* 4. Every telemetry cycle: drain packets; drop old encounters. */
static void tick(Spacecraft *sc, int64_t now)
{
    uint8_t pkt[SB_FSW_TM_MAX_BYTES];
    uint32_t n;
    while ((n = sb_fsw_poll_tm(&sc->fsw, pkt, sizeof(pkt), now)) != 0U)
    {
        downlink_send(sc, pkt, n);
    }
    sb_fsw_expire(&sc->fsw, now, 86400000);
    sb_flock_expire(&sc->flock, now, 86400000);
}

int main(int argc, char **argv)
{
    static char cdm_text[65536];
    static Spacecraft a, b, c;
    static SbCdm cdm;
    uint32_t len;
    uint32_t line;
    uint32_t i;
    int64_t now;
    FILE *f = fopen((argc > 1) ? argv[1] : "examples/cdm/crossing_demo.cdm",
                    "rb");
    if (f == NULL)
    {
        fprintf(stderr, "usage: flight_loop CDM_FILE\n");
        return 2;
    }
    len = (uint32_t)fread(cdm_text, 1U, sizeof(cdm_text), f);
    fclose(f);
    if (sb_cdm_parse(cdm_text, len, &cdm, &line) != SB_CDM_OK)
    {
        fprintf(stderr, "not a usable CDM\n");
        return 1;
    }

    a.name = "SAT-A";
    b.name = "SAT-B";
    c.name = "SAT-C";
    {   /* The two craft named in the warning, and a neighbour. */
        char ida[13], idb[13];
        memcpy(ida, cdm.obj[0].id.c, 12U); ida[12] = '\0';
        memcpy(idb, cdm.obj[1].id.c, 12U); idb[12] = '\0';
        if (!boot(&a, ida, SB_MODE_AUTONOMOUS) ||
            !boot(&b, idb, SB_MODE_AUTONOMOUS) ||
            !boot(&c, "2026-999X", SB_MODE_ADVISORY))
        {
            return 1;
        }
    }

    now = cdm.tca_gps_ms - 6LL * 3600LL * 1000LL;     /* 6 h before TCA */
    printf("\nwarning %s uplinked to both craft:\n", cdm.message_id);
    on_cdm(&a, cdm_text, len, now);
    on_cdm(&b, cdm_text, len, now);
    on_cdm(&c, cdm_text, len, now);                   /* not named: ignored */

    printf("\nneighbour SAT-C hears the inter-satellite link:\n");
    for (i = 0U; i < isl_count; ++i)
    {
        /* SAT-C's own state (from its navigation): here, 5 km ahead of
         * object 1 along-track at the time of closest approach. */
        CraftState own = cdm.obj[0].state_tca;
        own.position = sb_vadd(own.position,
                               sb_vscale(own.velocity,
                                         5000.0 / sb_vnorm(own.velocity)));
        on_isl(&c, isl_air[i], own, cdm.tca_gps_ms);
    }

    printf("\ntelemetry cycle:\n");
    tick(&a, now + 1000);
    tick(&b, now + 1000);
    printf("\nSAT-C now tracks %u maneuvers it was never warned about "
           "(ledger digest %016llX)\n", (unsigned)c.flock.received,
           (unsigned long long)sb_flock_digest(&c.flock));
    return 0;
}
