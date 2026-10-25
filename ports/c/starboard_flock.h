// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// starboard_flock.h — Layer 4, THE FLOCK (SPEC §7.7, PROTOCOL.md §6).
//
// Like starlings in a murmuration, every Starboard craft keeps a live picture
// of what its neighbours are about to do:
//
//   1. When a craft commits to a give-way maneuver it emits a MANEUVER NOTICE
//      (204 bytes): who moves, when, by how much, its post-burn trajectory,
//      and the exact burns that will bring it back to its slot.
//   2. Notices spread hop by hop over inter-satellite links / shared relays
//      (gossip: relay each notice once, with a hop limit), so they reach the
//      whole constellation in "diameter" hops — a ripple through the flock.
//   3. Every craft folds every notice into its FLOCK LEDGER (fixed size,
//      de-duplicated by (origin, sequence)), so after the ripple all ledgers
//      hold the SAME set: one collective picture of the flock's motion.
//   4. Each craft checks every mover's announced trajectory against its own
//      (flock_check) and raises a FLOCK ALERT if a neighbour's maneuver or
//      return would bring it inside the safety radius.
//
// Safety posture, unchanged from Layer 2: the flock CONFIRMS and WARNS, it
// never DECIDES. A notice cannot make a craft maneuver — an alert requests a
// fresh CDM and notifies the ground; the deterministic verdict still comes
// from shared data. Notices are also redundant by construction: the give-way
// burn is a pure function of the shared CDM, so any craft holding that CDM
// already knows the burn without receiving a byte ("the silent flock"). A
// forged or corrupted notice can therefore cost at most a re-check.

#ifndef STARBOARD_FLOCK_H
#define STARBOARD_FLOCK_H

#include "starboard.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SB_NOTICE_BYTES       (204U)
#define SB_NOTICE_VERSION     (1U)
#define SB_FLOCK_MAX_ENTRIES  (64U)
#define SB_FLOCK_DEFAULT_HOPS (16U)
#define SB_FLOCK_STEP_S       (4.0)     /* check grid = PROP_STEP_S */
#define SB_FLOCK_MAX_WINDOW_S (32768.0) /* <= 9.1 h checked per notice */

typedef struct {
    uint8_t    hops;              /* remaining relay hops                  */
    ObjectId   origin;            /* the maneuvering craft                 */
    uint32_t   seq;               /* origin's notice counter               */
    uint64_t   conjunction_id;    /* the encounter that caused the burn    */
    ObjectId   counterpart;       /* what it is avoiding                   */
    int64_t    tca_epoch_ms;
    int64_t    burn_epoch_ms;     /* GPS ms                                */
    CraftState post_burn;         /* canonical state just after the burn   */
    Vec3       dv_eci;
    int64_t    mirror_epoch_ms;   /* return burn 1 (Hill frame of origin)  */
    Vec3       mirror_dv_hill;
    int64_t    final_epoch_ms;    /* return burn 2 — back in slot          */
    Vec3       final_dv_hill;
} SbNotice;

typedef enum {
    SB_FLOCK_NEW = 0,         /* stored; relay it (hops permitting)       */
    SB_FLOCK_UPDATED = 1,     /* newer seq for a known origin+encounter   */
    SB_FLOCK_DUPLICATE = 2,   /* already known: do not relay              */
    SB_FLOCK_REJECTED = 3,    /* malformed / CRC / domain / own notice    */
    SB_FLOCK_FULL = 4         /* ledger full of live entries              */
} SbFlockResult;

typedef struct {
    bool     used;
    SbNotice n;
} SbFlockEntry;

typedef struct {
    ObjectId     self_id;
    uint32_t     received;
    uint32_t     relayed;
    uint32_t     duplicates;
    uint32_t     rejected;
    SbFlockEntry e[SB_FLOCK_MAX_ENTRIES];
} SbFlock;

typedef struct {
    bool    conflict;         /* min separation < R_SAFE                  */
    double  min_sep_m;        /* closest approach mover <-> self          */
    int64_t at_epoch_ms;      /* when                                     */
} SbFlockCheck;

/* Fixed little-endian encoding with a CRC-32 trailer (PROTOCOL.md §6). */
uint32_t sb_notice_encode(const SbNotice *n, uint8_t out[SB_NOTICE_BYTES]);
bool     sb_notice_decode(const uint8_t *in, uint32_t len, SbNotice *out);

/* Build the notice for a give-way plan. `self_tca` is the craft's canonical
 * state at TCA; the burn-time state is its canonical back-propagation, so
 * every holder of the CDM reproduces this notice bit for bit. */
void sb_notice_from_plan(const ObjectId *self, const ObjectId *counterpart,
                         uint32_t seq, uint64_t conjunction_id,
                         int64_t tca_epoch_ms, int64_t burn_epoch_ms,
                         CraftState self_tca, const GiveWayPlan *plan,
                         const MirrorReturnPlan *ret, SbNotice *out);

void sb_flock_init(SbFlock *f, ObjectId self_id);

/* Fold a received notice into the ledger. On NEW/UPDATED with hops left,
 * `relay` receives the bytes to forward (hops decremented) and *relay_len
 * is set; otherwise *relay_len = 0. */
SbFlockResult sb_flock_receive(SbFlock *f, const uint8_t *in, uint32_t len,
                               uint8_t relay[SB_NOTICE_BYTES],
                               uint32_t *relay_len);

/* Drop entries whose return completed more than retain_ms ago. */
void sb_flock_expire(SbFlock *f, int64_t now_gps_ms, int64_t retain_ms);

/* Check a mover's announced trajectory (burn, coast, mirror, phase, final)
 * against own canonical state `own` at `own_epoch_ms`, over the mover's
 * maneuver window (bounded by SB_FLOCK_MAX_WINDOW_S). */
SbFlockCheck sb_flock_check(const SbNotice *n, CraftState own,
                            int64_t own_epoch_ms);

/* As sb_flock_check, but `own_plan` (if non-NULL and burning at the same
 * epoch as `n`) makes own craft fly its own announced schedule too — used to
 * verify a FORMATION maneuver in unison (§7.7). */
SbFlockCheck sb_flock_check_pair(const SbNotice *n, const SbNotice *own_plan,
                                 CraftState own, int64_t own_epoch_ms);

/* Order-independent digest of the ledger contents: equal digests on two
 * craft mean they hold the same collective picture. */
uint64_t sb_flock_digest(const SbFlock *f);

#ifdef __cplusplus
}
#endif

#endif /* STARBOARD_FLOCK_H */
