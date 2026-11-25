// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// starboard_easy.h — THE FIVE-MINUTE API.
//
// You have a collision warning (a CCSDS Conjunction Data Message, the file
// every space-tracking provider sends) and you want to know: do I move?
//
//     SbAdvice a;
//     sb_easy_decide(cdm_text, cdm_len, "2026-001A", SB_SAT_SMALLSAT, 0, &a);
//     if (a.action == SB_ACTION_MOVE)  { ... burn a.dv_rtn_mps at a.burn_gps_ms }
//     puts(a.summary);    // one plain-English sentence for the operator
//
// That is the whole integration. The other satellite, running Starboard
// with its own copy of the same warning, reaches the mirror-image answer:
// exactly one of you moves, decided the same way, bit for bit.
//
// Everything below is a thin, allocation-free layer over the flight library
// (starboard_fsw.h). When you need the full flight features (encounter
// table, SEU-tolerant redundancy, CCSDS telemetry, flock notices), use
// starboard_fsw.h directly; the decisions are identical.

#ifndef STARBOARD_EASY_H
#define STARBOARD_EASY_H

#include "starboard_fsw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pick the profile closest to your spacecraft. It sets the defaults that
 * matter: whether you can maneuver, your priority tier, and how far ahead
 * of the closest approach you prefer to burn. */
typedef enum {
    SB_SAT_NO_PROPULSION = 0,  /* cannot move: Starboard tells the other craft
                                  to give way, and alerts you if it can't    */
    SB_SAT_CUBESAT       = 1,  /* small thruster, commanded via ground passes */
    SB_SAT_SMALLSAT      = 2,  /* propulsive small/medium satellite           */
    SB_SAT_CONSTELLATION = 3,  /* autonomous constellation member (Starlink-
                                  class): short command latency              */
    SB_SAT_HIGH_VALUE    = 4,  /* critical-infrastructure tier (e.g. GNSS,
                                  weather, science flagships)                */
    SB_SAT_CREWED        = 5   /* crewed vehicle or station: highest tier     */
} SbSatProfile;

#define SB_SAT_PROFILE_COUNT (6U)

typedef enum {
    SB_ACTION_ERROR   = -1, /* the warning could not be read: see error_text.
                               Fail-safe: this is NEVER "all clear".         */
    SB_ACTION_NOTHING = 0,  /* no meaningful risk: carry on                  */
    SB_ACTION_MOVE    = 1,  /* you give way: perform the burn below          */
    SB_ACTION_HOLD    = 2,  /* the other craft gives way: keep your course
                               steady so it can predict you                  */
    SB_ACTION_ALERT   = 3   /* you should give way but cannot maneuver:
                               alert your operators now                      */
} SbAction;

typedef struct {
    SbAction    action;
    char        summary[256];      /* one plain-English sentence             */

    /* Identification */
    char        self_id[13];       /* canonical ids (international designator) */
    char        other_id[13];
    char        other_name[40];
    uint64_t    conjunction_id;    /* identical on both spacecraft           */

    /* Risk (Starboard's own canonical computation, not the provider's) */
    double      miss_m;            /* predicted miss distance                */
    double      pc;                /* probability of collision               */
    int64_t     tca_gps_ms;        /* time of closest approach, GPS ms       */

    /* Decision */
    int         rule;              /* 1..5 which rule decided (0 if none)    */
    const char *rule_text;         /* e.g. "starboard rule"                  */
    int         advisory;          /* 1: nobody verified to act; operators
                                      should confirm                         */

    /* The burn (action == SB_ACTION_MOVE) */
    int64_t     burn_gps_ms;       /* when to burn, GPS ms                   */
    double      dv_rtn_mps[3];     /* radial, along-track, cross-track m/s   */
    double      dv_mps;            /* magnitude, m/s                         */
    double      return_dv_mps;     /* to regain your slot afterwards, m/s    */
    int         late;              /* 1: less lead time than preferred       */

    /* Errors (action == SB_ACTION_ERROR) */
    int         error;             /* SbCdmStatus, or 100 + SbFswStatus      */
    uint32_t    error_line;        /* 1-based line in the CDM, if known      */
    const char *error_text;
} SbAdvice;

/* Decide one collision warning.
 *   cdm, len     the CDM text (KVN), not necessarily NUL-terminated
 *   me           which object is you: its international designator
 *                ("2026-001A"), catalog number ("58123") or OBJECT_NAME.
 *                NULL means OBJECT1 (useful on the ground).
 *   profile      your spacecraft type (above)
 *   now_gps_ms   current time in GPS milliseconds; 0 = assume 6 hours
 *                before closest approach (handy for testing)
 * When you give way, the burn is the cheapest one in your profile's lead
 * window (searched on a fixed 5-minute grid, so anyone holding the same
 * warning and knowing your profile computes the same burn).
 * Returns out->action. Pure and deterministic; no heap, no globals. */
SbAction sb_easy_decide(const char *cdm, uint32_t len, const char *me,
                        SbSatProfile profile, int64_t now_gps_ms,
                        SbAdvice *out);

/* Fill a flight-layer configuration from a profile (for starboard_fsw.h
 * users who want the same defaults). self_id is left zeroed. The flight
 * layer burns at the fixed preferred lead (~45 min); only sb_easy_decide
 * searches the window. */
void sb_easy_profile_config(SbSatProfile profile, SbFswConfig *cfg);

/* Run the built-in self-test once on this platform/build. Returns 0 if the
 * platform computes bit-identically to every other Starboard craft; any
 * other value means DO NOT USE this build (see SB_BIT_* in starboard_fsw.h;
 * the usual cause is a missing -ffp-contract=off). */
uint32_t sb_easy_selftest(void);

/* sizeof(SbAdvice), so language bindings can check their struct layout. */
uint32_t sb_easy_advice_size(void);

/* Short names, for logs and UIs. */
const char *sb_easy_action_str(SbAction a);
const char *sb_easy_profile_str(SbSatProfile p);

#ifdef __cplusplus
}
#endif

#endif /* STARBOARD_EASY_H */
