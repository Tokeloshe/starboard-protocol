// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// starboard_cdm.h — the CDM binding (SPEC §3.3a): CCSDS 508.0-B-1
// Conjunction Data Message (KVN) -> Starboard's canonical shared input.
//
// Two craft that receive the same CDM bytes MUST derive bit-identical
// shared inputs, on any libc. Hence: numbers are converted by a pinned exact
// decimal ingest (no strtod), epochs by integer calendar arithmetic, frames by
// a pinned rotation, and the RTN covariance by a pinned expansion order. All
// of it is allocation-free, recursion-free and bounded by the input length.

#ifndef STARBOARD_CDM_H
#define STARBOARD_CDM_H

#include "starboard.h"

#ifdef __cplusplus
extern "C" {
#endif

/* §3.3a pinned constants of the binding. */
#define SB_CDM_OMEGA_EARTH_RADS (7.292115e-5)   /* IERS nominal, pinned */
#define SB_CDM_BACKPROP_S       (512.0)         /* TCA anchor -> t_ref    */
#define SB_CDM_MAX_SIG_DIGITS   (15)            /* exact-ingest bound     */
#define SB_CDM_MAX_LINE         (512U)
#define SB_GPS_UNIX_OFFSET_MS   (315964800000LL) /* 1980-01-06 - 1970-01-01 */

typedef enum {
    SB_CDM_OK = 0,
    SB_CDM_ERR_SYNTAX = 1,      /* malformed line / not KVN                 */
    SB_CDM_ERR_NUMBER = 2,      /* not a decimal, >15 sig. digits, |e|>22   */
    SB_CDM_ERR_UNITS = 3,       /* bracketed units disagree with the field  */
    SB_CDM_ERR_MISSING = 4,     /* a mandatory field is absent              */
    SB_CDM_ERR_DUPLICATE = 5,   /* a field appears twice in one section     */
    SB_CDM_ERR_FRAME = 6,       /* REF_FRAME not supported by the binding   */
    SB_CDM_ERR_TIME = 7,        /* bad or out-of-range epoch                */
    SB_CDM_ERR_DOMAIN = 8,      /* value outside the SB/1 input domain      */
    SB_CDM_ERR_ID = 9,          /* no usable canonical identity             */
    SB_CDM_ERR_SAME_OBJECT = 10 /* both objects map to the same identity    */
} SbCdmStatus;

typedef enum {
    SB_FRAME_GCRF = 0,   /* GCRF / ICRF / EME2000: used as-is             */
    SB_FRAME_TEME = 1,   /* TEME (SGP4 frame): used as-is                 */
    SB_FRAME_ITRF = 2    /* ITRF: frozen-Earth rotation at TCA (§3.3a)     */
} SbFrame;

typedef enum {
    SB_MAN_UNKNOWN = 0,  /* N/A or absent */
    SB_MAN_YES = 1,
    SB_MAN_NO = 2
} SbManeuverable;

typedef enum {
    SB_OBJ_UNKNOWN = 0,
    SB_OBJ_PAYLOAD = 1,
    SB_OBJ_ROCKET_BODY = 2,
    SB_OBJ_DEBRIS = 3,
    SB_OBJ_OTHER = 4
} SbObjectType;

typedef struct {
    ObjectId       id;            /* canonical identity (§3.2)              */
    char           designator[16];/* OBJECT_DESIGNATOR (catalog number)      */
    char           name[40];      /* OBJECT_NAME, informative                */
    SbFrame        frame;         /* frame the CDM state was expressed in    */
    SbManeuverable maneuverable;  /* CDM claim: ASSERTED trust at most       */
    SbObjectType   object_type;
    CraftState     state_tca;     /* canonical inertial state AT TCA, lattice */
    Sym33          covariance;    /* inertial position covariance, lattice    */
} SbCdmObject;

typedef struct {
    char        message_id[48];
    char        originator[32];
    int64_t     tca_gps_ms;       /* TCA, GPS ms (anchor epoch)              */
    double      cdm_miss_m;       /* provider's MISS_DISTANCE, informative   */
    double      cdm_pc;           /* provider's Pc (or -1), informative      */
    double      hbr_m;            /* combined hard-body radius used          */
    SbCdmObject obj[2];
} SbCdm;

/* Parse a KVN CDM of `len` bytes (no NUL terminator required). On success
 * fills *out with the canonical shared input; on failure *out is zeroed,
 * the status says why, and *err_line (if non-NULL) is the 1-based line. */
SbCdmStatus sb_cdm_parse(const char *text, uint32_t len, SbCdm *out,
                         uint32_t *err_line);

/* The binding's screening step (§3.3a): anchor at the CDM TCA, back-
 * propagate both canonical states by SB_CDM_BACKPROP_S, then run the
 * canonical search over SCREEN_ARC_MAX_S. Fills *oc and the canonical TCA
 * epoch; returns whether a conjunction was detected. */
bool sb_cdm_screen(const SbCdm *cdm, OrbitConjunction *oc,
                   int64_t *tca_epoch_gps_ms);

/* Exact decimal ingest (§3.3a), exposed for conformance testing: parses
 * [sign] digits [. digits] [E|e [sign] digits] from s[0..len), applies the
 * extra power-of-ten `scale_exp` (e.g. +3 for km -> m), and returns the
 * correctly rounded binary64 value. */
SbCdmStatus sb_decimal_to_double(const char *s, uint32_t len, int scale_exp,
                                 double *out);

/* Civil UTC -> GPS ms with the pinned leap-second table (§3.3a). */
SbCdmStatus sb_utc_to_gps_ms(int32_t year, int32_t month, int32_t day,
                             int32_t hour, int32_t minute, int32_t sec,
                             int32_t millis, int64_t *out);

const char *sb_cdm_status_str(SbCdmStatus st);

#ifdef __cplusplus
}
#endif

#endif /* STARBOARD_CDM_H */
