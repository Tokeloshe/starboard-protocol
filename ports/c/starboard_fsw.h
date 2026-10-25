// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// starboard_fsw.h — the FLIGHT INTEGRATION LAYER (docs/FLIGHT.md).
//
// Everything a spacecraft's flight software needs around the protocol core,
// as one statically allocated context with a small C API:
//
//   * power-on / commanded BUILT-IN SELF-TEST (known-answer tests of the
//     arithmetic profile, both transcendentals, the J2 search, Pc, the rule
//     chain, SHA-512, Ed25519, the CDM binding, and a CRC over the constant
//     table) — any failure latches SAFE mode;
//   * fail-safe input handling: a CDM that cannot be bound is REJECTED and
//     reported, never treated as "no conjunction";
//   * SEU-tolerant evaluation: every decision is computed twice and compared
//     bit-for-bit (exact, because the computation is bit-deterministic);
//     a mismatch triggers a third run and a 2-of-3 vote, and a CRC guards
//     the stored input between ingest and every re-evaluation;
//   * operating modes: SAFE / ADVISORY (decide + report, never command a
//     burn — "shadow mode") / AUTONOMOUS (also emit maneuver requests);
//   * a bounded encounter table with rolling-CDM update semantics;
//   * CCSDS Space Packet telemetry (housekeeping, decision events,
//     maneuver requests) with a CRC-16 trailer.
//
// No heap, no recursion, no OS calls, no clock reads: time is an input.
// Every entry point is bounded in work by the table sizes below.

#ifndef STARBOARD_FSW_H
#define STARBOARD_FSW_H

#include "starboard.h"
#include "starboard_cdm.h"
#include "starboard_flock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SB_FSW_VERSION          (0x00060000U)   /* 0.6.0 */
#define SB_FSW_MAX_ENCOUNTERS   (16U)
#define SB_FSW_MAX_COUNTERPARTS (32U)
#define SB_FSW_TM_MAX_BYTES     (256U)

typedef enum {
    SB_MODE_BOOT = 0,        /* before the first self-test                    */
    SB_MODE_SAFE = 1,        /* self-test failed or fault: no decisions       */
    SB_MODE_ADVISORY = 2,    /* decide + telemeter; never request a burn      */
    SB_MODE_AUTONOMOUS = 3   /* also emit maneuver requests for give-way      */
} SbMode;

typedef enum {
    SB_FSW_OK = 0,
    SB_FSW_ERR_MODE = 1,          /* not allowed in the current mode          */
    SB_FSW_ERR_CDM = 2,           /* CDM rejected by the binding (see detail) */
    SB_FSW_ERR_NOT_INVOLVED = 3,  /* CDM does not name this spacecraft        */
    SB_FSW_ERR_TABLE_FULL = 4,    /* encounter table full                      */
    SB_FSW_ERR_STALE = 5,         /* TCA already in the past                   */
    SB_FSW_ERR_FAULT = 6,         /* redundant evaluation could not agree      */
    SB_FSW_ERR_ARG = 7            /* invalid argument                          */
} SbFswStatus;

/* Self-test bits (set = FAILED). */
#define SB_BIT_ARITH_FMA     (1U << 0)   /* FMA contraction detected         */
#define SB_BIT_ARITH_EXCESS  (1U << 1)   /* x87-style excess precision       */
#define SB_BIT_TRANSCEND     (1U << 2)   /* canon_exp / canon_sincos anchors */
#define SB_BIT_SEARCH        (1U << 3)   /* J2 search: TCA / miss / Pc bits  */
#define SB_BIT_VERDICT       (1U << 4)   /* rule chain + conjunction id      */
#define SB_BIT_MANEUVER      (1U << 5)   /* CW give-way plan bits            */
#define SB_BIT_SHA512        (1U << 6)
#define SB_BIT_ED25519       (1U << 7)
#define SB_BIT_CDM           (1U << 8)   /* CDM binding known answer         */
#define SB_BIT_CONSTANTS     (1U << 9)   /* CRC of the canonical constants   */
#define SB_BIT_ARITH_ROUND   (1U << 10)  /* IEEE-754 rounding probes         */

typedef enum {
    SB_ROLE_NONE = 0,        /* not actionable / no conjunction found         */
    SB_ROLE_GIVE_WAY = 1,
    SB_ROLE_STAND_ON = 2
} SbFswRole;

typedef struct {
    ObjectId self_id;
    Tier     self_tier;
    bool     self_can_maneuver;
    SbMode   operating_mode;       /* ADVISORY or AUTONOMOUS after self-test */
    uint16_t apid_base;            /* HK = base, EVENT = base+1, MNVR = base+2 */
    int64_t  burn_lead_ms;         /* preferred burn lead before TCA          */
    int64_t  cmd_latency_ms;       /* earliest burn = now + latency           */
} SbFswConfig;

/* One decision: a pure, bit-deterministic function of (CDM, counterpart
 * trust, config, now). Compared with sb_fsw_decision_equal (field by field,
 * never raw memcmp: padding bytes are not preserved by struct copies). */
typedef struct {
    uint8_t          found;           /* canonical search found a conjunction  */
    uint8_t          actionable;      /* §4.7                                  */
    uint8_t          role;            /* SbFswRole                             */
    uint8_t          rule;            /* DecidingRule (0 if none)              */
    uint8_t          advisory;
    uint8_t          late;            /* burn lead below the preferred lead    */
    uint8_t          self_index;      /* 0/1: which CDM object is self         */
    uint8_t          reserved;
    uint64_t         conjunction_id;
    int64_t          tca_epoch_ms;    /* GPS ms                                */
    int64_t          burn_epoch_ms;   /* GPS ms (0 if no burn)                 */
    double           miss_m;
    double           pc;
    GiveWayPlan      plan;
    MirrorReturnPlan ret;
} SbDecision;

typedef struct {
    ObjectId   id;
    TrustLevel trust;
    Tier       tier;
    bool       can_maneuver;
    bool       used;
} SbCounterpart;

typedef struct {
    bool       used;
    uint8_t    pending_tm;            /* bit0 event, bit1 maneuver request  */
    uint32_t   input_crc;             /* CRC-32 of the stored SbCdm          */
    SbCdm      cdm;
    SbDecision decision;
} SbEncounter;

typedef struct {
    SbFswConfig   cfg;
    SbMode        mode;
    uint32_t      selftest_failed;    /* SB_BIT_* of the latest self-test   */
    uint32_t      cdm_accepted;
    uint32_t      cdm_rejected;
    uint32_t      evaluations;
    uint32_t      seu_corrected;      /* redundant runs disagreed, 2/3 vote  */
    uint32_t      seu_uncorrected;    /* no majority: fault                  */
    uint32_t      input_corruptions;  /* stored-input CRC mismatches         */
    uint32_t      maneuver_requests;
    uint8_t       last_error;         /* SbFswStatus                         */
    uint8_t       last_cdm_error;     /* SbCdmStatus                         */
    uint16_t      tm_seq;             /* CCSDS sequence count (14 bits)      */
    uint32_t      notice_seq;         /* flock notice counter (Layer 4)      */
    bool          hk_pending;
    SbCounterpart counterparts[SB_FSW_MAX_COUNTERPARTS];
    SbEncounter   enc[SB_FSW_MAX_ENCOUNTERS];
} SbFsw;

/* Initialize, run the self-test, and enter cfg->operating_mode (or SAFE). */
SbFswStatus sb_fsw_init(SbFsw *ctx, const SbFswConfig *cfg);

/* Run the built-in self-test; returns the failed-bit mask (0 = pass). A
 * failure latches SAFE; a pass returns a SAFE context to cfg mode. */
uint32_t sb_fsw_selftest(SbFsw *ctx);

/* Commanded mode change. Leaving SAFE requires a passing self-test. */
SbFswStatus sb_fsw_set_mode(SbFsw *ctx, SbMode mode);

/* Ingest one KVN CDM at time now_gps_ms: bind, screen, decide (redundantly)
 * and queue telemetry. Returns the encounter slot via *slot (may be NULL). */
SbFswStatus sb_fsw_ingest_cdm(SbFsw *ctx, const char *kvn, uint32_t len,
                              int64_t now_gps_ms, uint32_t *slot);

/* Record the registry outcome for a counterpart (output of
 * establish_trust) and re-decide every open encounter with it. */
SbFswStatus sb_fsw_set_counterpart(SbFsw *ctx, ObjectId id, TrustLevel trust,
                                   Tier tier, bool can_maneuver,
                                   int64_t now_gps_ms);

/* Drop encounters whose TCA is more than `retain_ms` in the past. */
void sb_fsw_expire(SbFsw *ctx, int64_t now_gps_ms, int64_t retain_ms);

/* Emit the next pending telemetry packet into buf (cap >= 256). Returns
 * the packet length, or 0 if nothing is pending. Order: HK, then events,
 * then maneuver requests. */
uint32_t sb_fsw_poll_tm(SbFsw *ctx, uint8_t *buf, uint32_t cap,
                        int64_t now_gps_ms);

/* Build the Layer-4 MANEUVER NOTICE for a give-way encounter, for the host
 * to broadcast on its inter-satellite links / relay (starboard_flock.h).
 * Returns SB_NOTICE_BYTES, or 0 if the slot holds no planned burn. */
uint32_t sb_fsw_notice(SbFsw *ctx, uint32_t slot,
                       uint8_t out[SB_NOTICE_BYTES]);

/* Request a housekeeping packet on the next poll. */
void sb_fsw_request_hk(SbFsw *ctx);

/* The pure decision function the layer runs redundantly (exposed so ground
 * tools compute exactly what the spacecraft computes). */
SbFswStatus sb_fsw_decide(const SbFswConfig *cfg, const SbCdm *cdm,
                          const SbCounterpart *other, int64_t now_gps_ms,
                          SbDecision *out);

/* Self-test known answers, exposed for ground tooling (`sbctl selftest
 * --print-kat`) and for regenerating the pinned values after a normative
 * change. */
uint64_t sb_fsw_kat_cdm_fold(void);
uint64_t sb_fsw_transcendental_fold(void);
uint32_t sb_fsw_constants_crc(void);

/* Field-by-field bit equality of two decisions (padding-independent). */
bool sb_fsw_decision_equal(const SbDecision *a, const SbDecision *b);

/* CCSDS helpers (ccsds.c). */
uint16_t sb_crc16_ccitt(const uint8_t *data, uint32_t len);
uint32_t sb_crc32(const uint8_t *data, uint32_t len);
uint32_t sb_ccsds_tm_packet(uint8_t *buf, uint32_t cap, uint16_t apid,
                            uint16_t seq, int64_t time_gps_ms,
                            const uint8_t *payload, uint32_t plen);

const char *sb_fsw_status_str(SbFswStatus st);

#ifdef __cplusplus
}
#endif

#endif /* STARBOARD_FSW_H */
