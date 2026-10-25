// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// cdm.c — the CDM binding (SPEC §3.3a). CCSDS 508.0-B-1 KVN conjunction data
// message -> canonical shared input. See starboard_cdm.h for the contract.
//
// Determinism notes (normative, SPEC §3.3a):
//  * Numbers: exact decimal ingest. A field is accepted only if it has at
//    most 15 significant digits and, after folding in the unit's power of
//    ten, a decimal exponent |e| <= 22. Then M (< 10^15 < 2^53) and 10^|e|
//    (<= 10^22) are both exact binary64 values and ONE IEEE multiply or
//    divide yields the correctly rounded result (Clinger 1990, "fast path").
//    No strtod: libc decimal conversion is not uniformly correctly rounded.
//  * Epochs: integer civil-calendar arithmetic + the pinned leap table.
//  * Frames: ITRF is mapped by the frozen-Earth rotation at TCA,
//    v_i = v_f + w x r_f with w = (0, 0, OMEGA_EARTH); GCRF/ICRF/EME2000 and
//    TEME pass through. Pinned operation order.
//  * Covariance: RTN -> inertial, C = B^T K B with B the Hill basis of the
//    canonical state, pinned expansion order, then lattice-quantized.

#include "starboard_cdm.h"
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Small text helpers (bounded, no libc ctype: locale-free by construction) */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *p;
    uint32_t    n;
} Span;

static bool is_space(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\f') ||
           (c == '\v');
}

static bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

static char to_upper(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - ('a' - 'A')) : c;
}

static Span span_trim(Span s)
{
    while ((s.n > 0U) && is_space(s.p[0]))
    {
        ++s.p;
        --s.n;
    }
    while ((s.n > 0U) && is_space(s.p[s.n - 1U]))
    {
        --s.n;
    }
    return s;
}

static bool span_eq(Span s, const char *lit)
{
    uint32_t i = 0U;
    while (lit[i] != '\0')
    {
        if ((i >= s.n) || (s.p[i] != lit[i]))
        {
            return false;
        }
        ++i;
    }
    return i == s.n;
}

/* True iff s begins with the literal. */
static bool span_eq_prefix(Span s, const char *lit)
{
    uint32_t i = 0U;
    while (lit[i] != '\0')
    {
        if ((i >= s.n) || (s.p[i] != lit[i]))
        {
            return false;
        }
        ++i;
    }
    return true;
}

static bool span_ieq(Span s, const char *lit)
{
    uint32_t i = 0U;
    while (lit[i] != '\0')
    {
        if ((i >= s.n) || (to_upper(s.p[i]) != to_upper(lit[i])))
        {
            return false;
        }
        ++i;
    }
    return i == s.n;
}

static bool span_copy(Span s, char *dst, uint32_t cap)
{
    uint32_t i;
    if (s.n >= cap)
    {
        return false;
    }
    for (i = 0U; i < s.n; ++i)
    {
        dst[i] = s.p[i];
    }
    dst[s.n] = '\0';
    return true;
}

/* ------------------------------------------------------------------------ */
/* Exact decimal ingest                                                     */
/* ------------------------------------------------------------------------ */

static const double SB_POW10[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

SbCdmStatus sb_decimal_to_double(const char *s, uint32_t len, int scale_exp,
                                 double *out)
{
    uint32_t i = 0U;
    bool neg = false;
    bool any_digit = false;
    bool started = false;      /* a nonzero mantissa digit has been seen */
    bool in_frac = false;
    uint64_t m = 0U;
    int sig = 0;
    int pending = 0;           /* zeros seen after the last nonzero digit */
    int frac_len = 0;
    int32_t e10;
    int32_t exp_val = 0;
    double mag;

    *out = 0.0;
    if ((i < len) && ((s[i] == '+') || (s[i] == '-')))
    {
        neg = (s[i] == '-');
        ++i;
    }
    while (i < len)
    {
        const char c = s[i];
        if (is_digit(c))
        {
            any_digit = true;
            if (in_frac)
            {
                ++frac_len;
            }
            if (c == '0')
            {
                if (started)
                {
                    ++pending;
                }
            }
            else
            {
                while (pending > 0)
                {
                    m = m * 10U;
                    ++sig;
                    --pending;
                    if (sig > SB_CDM_MAX_SIG_DIGITS)
                    {
                        return SB_CDM_ERR_NUMBER;
                    }
                }
                m = (m * 10U) + (uint64_t)(uint8_t)(c - '0');
                ++sig;
                started = true;
                if (sig > SB_CDM_MAX_SIG_DIGITS)
                {
                    return SB_CDM_ERR_NUMBER;
                }
            }
            ++i;
        }
        else if ((c == '.') && (!in_frac))
        {
            in_frac = true;
            ++i;
        }
        else
        {
            break;
        }
    }
    if (!any_digit)
    {
        return SB_CDM_ERR_NUMBER;
    }
    if ((i < len) && ((s[i] == 'e') || (s[i] == 'E')))
    {
        bool eneg = false;
        int ndig = 0;
        ++i;
        if ((i < len) && ((s[i] == '+') || (s[i] == '-')))
        {
            eneg = (s[i] == '-');
            ++i;
        }
        while ((i < len) && is_digit(s[i]))
        {
            if (ndig >= 4)
            {
                return SB_CDM_ERR_NUMBER;
            }
            exp_val = (exp_val * 10) + (int32_t)(s[i] - '0');
            ++ndig;
            ++i;
        }
        if (ndig == 0)
        {
            return SB_CDM_ERR_NUMBER;
        }
        if (eneg)
        {
            exp_val = -exp_val;
        }
    }
    if (i != len)
    {
        return SB_CDM_ERR_NUMBER;   /* trailing garbage */
    }
    if (m == 0U)
    {
        *out = neg ? -0.0 : 0.0;
        return SB_CDM_OK;
    }
    /* Trailing zeros of the integer part shift the exponent up; trailing
     * zeros of the fraction are already counted in frac_len. */
    e10 = (exp_val + (int32_t)pending) - (int32_t)frac_len +
          (int32_t)scale_exp;
    if ((e10 > 22) || (e10 < -22))
    {
        return SB_CDM_ERR_NUMBER;
    }
    mag = (double)m;            /* exact: m < 10^15 < 2^53 */
    if (e10 >= 0)
    {
        mag = mag * SB_POW10[e10];
    }
    else
    {
        mag = mag / SB_POW10[-e10];
    }
    *out = neg ? -mag : mag;
    return SB_CDM_OK;
}

/* ------------------------------------------------------------------------ */
/* Time: civil UTC -> GPS ms                                                */
/* ------------------------------------------------------------------------ */

static int64_t days_from_civil(int32_t y_in, int32_t m, int32_t d)
{
    int32_t era;
    int32_t yoe;
    int32_t doy;
    int32_t doe;
    const int32_t y = y_in - ((m <= 2) ? 1 : 0);
    era = ((y >= 0) ? y : (y - 399)) / 400;
    yoe = y - (era * 400);
    doy = (((153 * (m + ((m > 2) ? -3 : 9))) + 2) / 5) + (d - 1);
    doe = (((yoe * 365) + (yoe / 4)) - (yoe / 100)) + doy;
    return ((int64_t)era * 146097) + (int64_t)doe - 719468;
}

static bool is_leap_year(int32_t y)
{
    return (((y % 4) == 0) && ((y % 100) != 0)) || ((y % 400) == 0);
}

static int32_t days_in_month(int32_t y, int32_t m)
{
    static const int32_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31,
                                    30, 31};
    if ((m == 2) && is_leap_year(y))
    {
        return 29;
    }
    return dim[m - 1];
}

/* GPS - UTC steps: (year, month) at whose 1st day 00:00 UTC the offset
 * becomes index+1 seconds. Pinned through the 2017-01-01 leap second; no
 * leap second has been inserted since. A new IERS leap second is a binding
 * revision (it changes the bytes -> epoch map). */
static const int32_t SB_LEAP_YM[18][2] = {
    {1981, 7}, {1982, 7}, {1983, 7}, {1985, 7}, {1988, 1}, {1990, 1},
    {1991, 1}, {1992, 7}, {1993, 7}, {1994, 7}, {1996, 1}, {1997, 7},
    {1999, 1}, {2006, 1}, {2009, 1}, {2012, 7}, {2015, 7}, {2017, 1}
};

SbCdmStatus sb_utc_to_gps_ms(int32_t year, int32_t month, int32_t day,
                             int32_t hour, int32_t minute, int32_t sec,
                             int32_t millis, int64_t *out)
{
    int64_t unix_ms;
    int64_t leap = 0;
    uint32_t k;
    *out = 0;
    if ((year < 1980) || (year > 2200) || (month < 1) || (month > 12) ||
        (day < 1) || (day > days_in_month(year, month)) || (hour < 0) ||
        (hour > 23) || (minute < 0) || (minute > 59) || (sec < 0) ||
        (sec > 59) || (millis < 0) || (millis > 1000))
    {
        return SB_CDM_ERR_TIME;
    }
    unix_ms = (((((((days_from_civil(year, month, day) * 24) + hour) * 60) +
                  minute) * 60) + sec) * 1000) + millis;
    for (k = 0U; k < 18U; ++k)
    {
        const int64_t step_ms =
            days_from_civil(SB_LEAP_YM[k][0], SB_LEAP_YM[k][1], 1) *
            86400000LL;
        if (unix_ms >= step_ms)
        {
            leap = (int64_t)k + 1;
        }
    }
    *out = (unix_ms - SB_GPS_UNIX_OFFSET_MS) + (leap * 1000);
    if (*out < 0)
    {
        *out = 0;
        return SB_CDM_ERR_TIME;
    }
    return SB_CDM_OK;
}

static bool parse_uint(const char *p, uint32_t n, int32_t *out)
{
    uint32_t i;
    int32_t v = 0;
    if ((n == 0U) || (n > 6U))
    {
        return false;
    }
    for (i = 0U; i < n; ++i)
    {
        if (!is_digit(p[i]))
        {
            return false;
        }
        v = (v * 10) + (int32_t)(p[i] - '0');
    }
    *out = v;
    return true;
}

/* "YYYY-MM-DDThh:mm:ss[.f+][Z]" or "YYYY-DDDThh:mm:ss[.f+][Z]". Fractional
 * seconds round half-to-even to the millisecond, exactly (digit compare). */
static SbCdmStatus parse_epoch(Span v, int64_t *gps_ms)
{
    const char *p = v.p;
    uint32_t n = v.n;
    int32_t y;
    int32_t mo;
    int32_t d;
    int32_t hh;
    int32_t mi;
    int32_t ss;
    int32_t ms = 0;
    uint32_t t_at;
    uint32_t i;
    if ((n > 0U) && ((p[n - 1U] == 'Z') || (p[n - 1U] == 'z')))
    {
        --n;
    }
    if ((n < 17U) || (p[4] != '-'))
    {
        return SB_CDM_ERR_TIME;
    }
    if (!parse_uint(p, 4U, &y))
    {
        return SB_CDM_ERR_TIME;
    }
    if ((n >= 19U) && (p[7] == '-') && (p[10] == 'T'))
    {
        if (!parse_uint(&p[5], 2U, &mo) || !parse_uint(&p[8], 2U, &d))
        {
            return SB_CDM_ERR_TIME;
        }
        t_at = 10U;
    }
    else if (p[8] == 'T')
    {
        int32_t doy;
        if (!parse_uint(&p[5], 3U, &doy) || (y < 1980) || (y > 2200))
        {
            return SB_CDM_ERR_TIME;
        }
        mo = 1;
        while ((mo <= 12) && (doy > days_in_month(y, mo)))
        {
            doy -= days_in_month(y, mo);
            ++mo;
        }
        if ((doy < 1) || (mo > 12))
        {
            return SB_CDM_ERR_TIME;
        }
        d = doy;
        t_at = 8U;
    }
    else
    {
        return SB_CDM_ERR_TIME;
    }
    if ((n < (t_at + 9U)) || (p[t_at + 3U] != ':') || (p[t_at + 6U] != ':'))
    {
        return SB_CDM_ERR_TIME;
    }
    if (!parse_uint(&p[t_at + 1U], 2U, &hh) ||
        !parse_uint(&p[t_at + 4U], 2U, &mi) ||
        !parse_uint(&p[t_at + 7U], 2U, &ss))
    {
        return SB_CDM_ERR_TIME;
    }
    i = t_at + 9U;
    if (i < n)
    {
        uint32_t nd = 0U;
        bool rest_nonzero = false;
        int32_t first_rest = -1;
        if (p[i] != '.')
        {
            return SB_CDM_ERR_TIME;
        }
        ++i;
        if (i >= n)
        {
            return SB_CDM_ERR_TIME;
        }
        for (; i < n; ++i)
        {
            if (!is_digit(p[i]))
            {
                return SB_CDM_ERR_TIME;
            }
            if (nd < 3U)
            {
                ms = (ms * 10) + (int32_t)(p[i] - '0');
            }
            else if (nd == 3U)
            {
                first_rest = (int32_t)(p[i] - '0');
            }
            else if (p[i] != '0')
            {
                rest_nonzero = true;
            }
            else
            {
                /* zero beyond the rounding digit */
            }
            ++nd;
        }
        while (nd < 3U)
        {
            ms = ms * 10;
            ++nd;
        }
        if ((first_rest > 5) || ((first_rest == 5) && rest_nonzero) ||
            ((first_rest == 5) && (!rest_nonzero) && ((ms % 2) == 1)))
        {
            ++ms;   /* may reach 1000: carries through the ms arithmetic */
        }
    }
    return sb_utc_to_gps_ms(y, mo, d, hh, mi, ss, ms, gps_ms);
}

/* ------------------------------------------------------------------------ */
/* KVN field table                                                          */
/* ------------------------------------------------------------------------ */

enum {
    F_VERS = 0, F_MSGID, F_ORIG, F_TCA, F_MISS, F_PC, F_HBR, F_HDR_COUNT
};

enum {
    O_DESIG = 0, O_NAME, O_INTL, O_TYPE, O_MAN, O_FRAME,
    O_X, O_Y, O_Z, O_XD, O_YD, O_ZD,
    O_CRR, O_CTR, O_CTT, O_CNR, O_CNT, O_CNN, O_COUNT
};

static const char *const HDR_KEYS[F_HDR_COUNT] = {
    "CCSDS_CDM_VERS", "MESSAGE_ID", "ORIGINATOR", "TCA", "MISS_DISTANCE",
    "COLLISION_PROBABILITY", "HBR"
};

static const char *const OBJ_KEYS[O_COUNT] = {
    "OBJECT_DESIGNATOR", "OBJECT_NAME", "INTERNATIONAL_DESIGNATOR",
    "OBJECT_TYPE", "MANEUVERABLE", "REF_FRAME",
    "X", "Y", "Z", "X_DOT", "Y_DOT", "Z_DOT",
    "CR_R", "CT_R", "CT_T", "CN_R", "CN_T", "CN_N"
};

/* Expected units (NULL: none), and the power of ten that converts the
 * CDM unit to Starboard's SI unit (km -> m: +3). */
static const char *const OBJ_UNITS[O_COUNT] = {
    NULL, NULL, NULL, NULL, NULL, NULL,
    "km", "km", "km", "km/s", "km/s", "km/s",
    "m**2", "m**2", "m**2", "m**2", "m**2", "m**2"
};

#define HDR_REQUIRED ((1U << F_VERS) | (1U << F_MSGID) | (1U << F_ORIG) | \
                      (1U << F_TCA) | (1U << F_MISS))
#define OBJ_REQUIRED ((1U << O_DESIG) | (1U << O_INTL) | (1U << O_MAN) | \
                      (1U << O_FRAME) | (1U << O_X) | (1U << O_Y) |      \
                      (1U << O_Z) | (1U << O_XD) | (1U << O_YD) |        \
                      (1U << O_ZD) | (1U << O_CRR) | (1U << O_CTR) |     \
                      (1U << O_CTT) | (1U << O_CNR) | (1U << O_CNT) |    \
                      (1U << O_CNN))

typedef struct {
    double     num[O_COUNT];
    Span       txt[O_COUNT];
    uint32_t   seen;
} ObjFields;

static int lookup(Span key, const char *const *table, int count)
{
    int i;
    for (i = 0; i < count; ++i)
    {
        if (span_eq(key, table[i]))
        {
            return i;
        }
    }
    return -1;
}

static bool units_ok(Span unit, const char *expected)
{
    if (unit.n == 0U)
    {
        return true;          /* units are optional in KVN */
    }
    if (expected == NULL)
    {
        return false;
    }
    return span_ieq(unit, expected);
}

/* Canonical identity (SPEC §3.2): the COSPAR international designator; if
 * the CDM marks it UNKNOWN (analyst objects), "CAT" + catalog number. */
static SbCdmStatus make_identity(const ObjFields *f, SbCdmObject *o)
{
    Span src = f->txt[O_INTL];
    char buf[13];
    uint32_t i;
    uint32_t len;
    if (span_ieq(src, "UNKNOWN") || (src.n == 0U))
    {
        Span d = f->txt[O_DESIG];
        if ((d.n == 0U) || (d.n > 9U))
        {
            return SB_CDM_ERR_ID;
        }
        buf[0] = 'C';
        buf[1] = 'A';
        buf[2] = 'T';
        for (i = 0U; i < d.n; ++i)
        {
            buf[3U + i] = d.p[i];
        }
        len = 3U + d.n;
        buf[len] = '\0';
    }
    else
    {
        if (src.n > 12U)
        {
            return SB_CDM_ERR_ID;
        }
        for (i = 0U; i < src.n; ++i)
        {
            buf[i] = src.p[i];
        }
        len = src.n;
        buf[len] = '\0';
    }
    /* Validate EVERY byte of the field, by length — not as a C string: an
     * embedded NUL would otherwise truncate "2026-001A\0<junk>" to a valid
     * identity (found by the C-vs-C++ differential fuzzer). */
    for (i = 0U; i < len; ++i)
    {
        const char c = to_upper(buf[i]);
        if (!(is_digit(c) || ((c >= 'A') && (c <= 'Z')) || (c == '-')))
        {
            return SB_CDM_ERR_ID;
        }
    }
    o->id = sb_make_object_id(buf);
    return SB_CDM_OK;
}

static Sym33 rtn_to_inertial(const Mat3 *b, double k[3][3])
{
    double mk[3][3];
    double c[3][3];
    int r;
    int j;
    Sym33 out;
    /* M = K B, then C = B^T M; sums ascend in the inner index. */
    for (r = 0; r < 3; ++r)
    {
        for (j = 0; j < 3; ++j)
        {
            mk[r][j] = ((k[r][0] * b->m[0][j]) + (k[r][1] * b->m[1][j])) +
                       (k[r][2] * b->m[2][j]);
        }
    }
    for (r = 0; r < 3; ++r)
    {
        for (j = r; j < 3; ++j)
        {
            c[r][j] = ((b->m[0][r] * mk[0][j]) + (b->m[1][r] * mk[1][j])) +
                      (b->m[2][r] * mk[2][j]);
        }
    }
    out.xx = c[0][0];
    out.xy = c[0][1];
    out.xz = c[0][2];
    out.yy = c[1][1];
    out.yz = c[1][2];
    out.zz = c[2][2];
    return sb_cov_quantize(out);
}

static SbCdmStatus bind_object(const ObjFields *f, SbCdmObject *o)
{
    Span fr = f->txt[O_FRAME];
    Span ty = f->txt[O_TYPE];
    Span mv = f->txt[O_MAN];
    CraftState st;
    double k[3][3];
    Mat3 basis;
    double rn;
    double vn;
    SbCdmStatus s = make_identity(f, o);
    if (s != SB_CDM_OK)
    {
        return s;
    }
    (void)span_copy(f->txt[O_DESIG], o->designator, sizeof(o->designator));
    if (!span_copy(f->txt[O_NAME], o->name, sizeof(o->name)))
    {
        o->name[0] = '\0';
    }
    if (span_eq(fr, "GCRF") || span_eq(fr, "ICRF") || span_eq(fr, "EME2000"))
    {
        o->frame = SB_FRAME_GCRF;
    }
    else if (span_eq(fr, "TEME"))
    {
        o->frame = SB_FRAME_TEME;
    }
    else if (span_eq(fr, "ITRF") || span_eq(fr, "ITRF2000") ||
             span_eq(fr, "ITRF-93") || span_eq(fr, "ITRF-97") ||
             span_eq(fr, "ITRF2008") || span_eq(fr, "ITRF2014") ||
             span_eq(fr, "ITRF2020"))
    {
        o->frame = SB_FRAME_ITRF;
    }
    else
    {
        return SB_CDM_ERR_FRAME;
    }
    if (span_eq(mv, "YES"))
    {
        o->maneuverable = SB_MAN_YES;
    }
    else if (span_eq(mv, "NO"))
    {
        o->maneuverable = SB_MAN_NO;
    }
    else if (span_eq(mv, "N/A"))
    {
        o->maneuverable = SB_MAN_UNKNOWN;
    }
    else
    {
        return SB_CDM_ERR_SYNTAX;
    }
    o->object_type = SB_OBJ_UNKNOWN;
    if (span_eq(ty, "PAYLOAD"))
    {
        o->object_type = SB_OBJ_PAYLOAD;
    }
    else if (span_eq(ty, "ROCKET BODY"))
    {
        o->object_type = SB_OBJ_ROCKET_BODY;
    }
    else if (span_eq(ty, "DEBRIS"))
    {
        o->object_type = SB_OBJ_DEBRIS;
    }
    else if (span_eq(ty, "OTHER"))
    {
        o->object_type = SB_OBJ_OTHER;
    }
    else
    {
        /* UNKNOWN / absent */
    }

    st.position.x = f->num[O_X];
    st.position.y = f->num[O_Y];
    st.position.z = f->num[O_Z];
    st.velocity.x = f->num[O_XD];
    st.velocity.y = f->num[O_YD];
    st.velocity.z = f->num[O_ZD];
    if (o->frame == SB_FRAME_ITRF)
    {
        /* Frozen-Earth rotation at TCA: v_i = v_f + w x r_f. */
        st.velocity.x = st.velocity.x -
                        (SB_CDM_OMEGA_EARTH_RADS * st.position.y);
        st.velocity.y = st.velocity.y +
                        (SB_CDM_OMEGA_EARTH_RADS * st.position.x);
    }
    o->state_tca = sb_canonicalize(st);

    /* SB/1 input domain (SPEC §3.7). */
    rn = sb_vnorm(o->state_tca.position);
    vn = sb_vnorm(o->state_tca.velocity);
    if ((rn < 6.0e6) || (rn > 5.0e7) || (vn < 1.0e3) || (vn > 2.0e4))
    {
        return SB_CDM_ERR_DOMAIN;
    }
    if ((f->num[O_CRR] < 0.0) || (f->num[O_CTT] < 0.0) ||
        (f->num[O_CNN] < 0.0) || (f->num[O_CRR] > 1.0e14) ||
        (f->num[O_CTT] > 1.0e14) || (f->num[O_CNN] > 1.0e14))
    {
        return SB_CDM_ERR_DOMAIN;
    }

    k[0][0] = f->num[O_CRR];
    k[0][1] = f->num[O_CTR];
    k[0][2] = f->num[O_CNR];
    k[1][0] = f->num[O_CTR];
    k[1][1] = f->num[O_CTT];
    k[1][2] = f->num[O_CNT];
    k[2][0] = f->num[O_CNR];
    k[2][1] = f->num[O_CNT];
    k[2][2] = f->num[O_CNN];
    basis = sb_hill_basis(o->state_tca);
    o->covariance = rtn_to_inertial(&basis, k);
    return SB_CDM_OK;
}

static int obj_scale_exp(int field)
{
    return ((field >= O_X) && (field <= O_ZD)) ? 3 : 0;
}

static bool obj_is_numeric(int field)
{
    return field >= O_X;
}

SbCdmStatus sb_cdm_parse(const char *text, uint32_t len, SbCdm *out,
                         uint32_t *err_line)
{
    ObjFields of[2];
    uint32_t hdr_seen = 0U;
    double hdr_num[F_HDR_COUNT];
    Span hdr_txt[F_HDR_COUNT];
    int section = 0;          /* 0 = header/relative, 1/2 = OBJECT1/2 */
    uint32_t pos = 0U;
    uint32_t line_no = 0U;
    SbCdmStatus st = SB_CDM_OK;
    int i;

    (void)memset(out, 0, sizeof(*out));
    (void)memset(of, 0, sizeof(of));
    (void)memset(hdr_num, 0, sizeof(hdr_num));
    (void)memset(hdr_txt, 0, sizeof(hdr_txt));
    if (err_line != NULL)
    {
        *err_line = 0U;
    }

    while ((pos < len) && (st == SB_CDM_OK))
    {
        Span line;
        Span key;
        Span val;
        Span unit;
        uint32_t eq;
        uint32_t end = pos;
        while ((end < len) && (text[end] != '\n'))
        {
            ++end;
        }
        ++line_no;
        line.p = &text[pos];
        line.n = end - pos;
        pos = (end < len) ? (end + 1U) : end;
        if (line.n > SB_CDM_MAX_LINE)
        {
            st = SB_CDM_ERR_SYNTAX;
            break;
        }
        line = span_trim(line);
        if (line.n == 0U)
        {
            continue;
        }
        if ((line.n >= 7U) && span_eq_prefix(line, "COMMENT") &&
            ((line.n == 7U) || is_space(line.p[7])))
        {
            continue;
        }
        eq = 0U;
        while ((eq < line.n) && (line.p[eq] != '='))
        {
            ++eq;
        }
        if (eq >= line.n)
        {
            st = SB_CDM_ERR_SYNTAX;
            break;
        }
        key.p = line.p;
        key.n = eq;
        key = span_trim(key);
        val.p = &line.p[eq + 1U];
        val.n = line.n - eq - 1U;
        val = span_trim(val);
        unit.p = val.p;
        unit.n = 0U;
        if ((val.n > 0U) && (val.p[val.n - 1U] == ']'))
        {
            uint32_t ob = val.n - 1U;
            while ((ob > 0U) && (val.p[ob] != '['))
            {
                --ob;
            }
            if (val.p[ob] != '[')
            {
                st = SB_CDM_ERR_SYNTAX;
                break;
            }
            unit.p = &val.p[ob + 1U];
            unit.n = val.n - ob - 2U;
            unit = span_trim(unit);
            val.n = ob;
            val = span_trim(val);
        }
        if ((key.n == 0U) || (val.n == 0U))
        {
            st = SB_CDM_ERR_SYNTAX;
            break;
        }

        if (span_eq(key, "OBJECT"))
        {
            if (span_eq(val, "OBJECT1") && (section == 0))
            {
                section = 1;
            }
            else if (span_eq(val, "OBJECT2") && (section == 1))
            {
                section = 2;
            }
            else
            {
                st = SB_CDM_ERR_SYNTAX;
            }
            continue;
        }

        if (section == 0)
        {
            const int f = lookup(key, HDR_KEYS, F_HDR_COUNT);
            if (f < 0)
            {
                continue;     /* optional / informative field: ignored */
            }
            if ((hdr_seen & (1U << (uint32_t)f)) != 0U)
            {
                st = SB_CDM_ERR_DUPLICATE;
                break;
            }
            hdr_seen |= (1U << (uint32_t)f);
            hdr_txt[f] = val;
            if ((f == F_MISS) || (f == F_HBR))
            {
                if (!units_ok(unit, "m"))
                {
                    st = SB_CDM_ERR_UNITS;
                    break;
                }
                st = sb_decimal_to_double(val.p, val.n, 0, &hdr_num[f]);
            }
            else if (f == F_PC)
            {
                if (unit.n != 0U)
                {
                    st = SB_CDM_ERR_UNITS;
                    break;
                }
                st = sb_decimal_to_double(val.p, val.n, 0, &hdr_num[f]);
            }
            else if (f == F_TCA)
            {
                st = parse_epoch(val, &out->tca_gps_ms);
            }
            else
            {
                /* text field */
            }
        }
        else
        {
            ObjFields *o = &of[section - 1];
            const int f = lookup(key, OBJ_KEYS, O_COUNT);
            if (f < 0)
            {
                continue;
            }
            if ((o->seen & (1U << (uint32_t)f)) != 0U)
            {
                st = SB_CDM_ERR_DUPLICATE;
                break;
            }
            o->seen |= (1U << (uint32_t)f);
            o->txt[f] = val;
            if (obj_is_numeric(f))
            {
                if (!units_ok(unit, OBJ_UNITS[f]))
                {
                    st = SB_CDM_ERR_UNITS;
                    break;
                }
                st = sb_decimal_to_double(val.p, val.n, obj_scale_exp(f),
                                          &o->num[f]);
            }
            else if (unit.n != 0U)
            {
                st = SB_CDM_ERR_UNITS;
            }
            else
            {
                /* text field */
            }
        }
    }

    if ((st == SB_CDM_OK) &&
        (((hdr_seen & HDR_REQUIRED) != HDR_REQUIRED) || (section != 2) ||
         ((of[0].seen & OBJ_REQUIRED) != OBJ_REQUIRED) ||
         ((of[1].seen & OBJ_REQUIRED) != OBJ_REQUIRED)))
    {
        st = SB_CDM_ERR_MISSING;
        line_no = 0U;
    }
    if (st == SB_CDM_OK)
    {
        out->cdm_miss_m = hdr_num[F_MISS];
        out->cdm_pc = ((hdr_seen & (1U << F_PC)) != 0U) ? hdr_num[F_PC] : -1.0;
        out->hbr_m = ((hdr_seen & (1U << F_HBR)) != 0U)
                         ? hdr_num[F_HBR]
                         : (2.0 * SB_DEFAULT_HBR_M);
        if (!(out->hbr_m > 0.0) || (out->hbr_m > 1.0e3) ||
            !(out->cdm_miss_m >= 0.0))
        {
            st = SB_CDM_ERR_DOMAIN;
        }
    }
    if (st == SB_CDM_OK)
    {
        (void)span_copy(hdr_txt[F_MSGID], out->message_id,
                        sizeof(out->message_id));
        (void)span_copy(hdr_txt[F_ORIG], out->originator,
                        sizeof(out->originator));
        for (i = 0; (i < 2) && (st == SB_CDM_OK); ++i)
        {
            st = bind_object(&of[i], &out->obj[i]);
        }
        line_no = 0U;
    }
    if ((st == SB_CDM_OK) &&
        sb_id_equal(&out->obj[0].id, &out->obj[1].id))
    {
        st = SB_CDM_ERR_SAME_OBJECT;
    }
    if (st != SB_CDM_OK)
    {
        (void)memset(out, 0, sizeof(*out));
        if (err_line != NULL)
        {
            *err_line = line_no;
        }
    }
    return st;
}

bool sb_cdm_screen(const SbCdm *cdm, OrbitConjunction *oc,
                   int64_t *tca_epoch_gps_ms)
{
    const CraftState a0 = sb_propagate(cdm->obj[0].state_tca,
                                       -SB_CDM_BACKPROP_S);
    const CraftState b0 = sb_propagate(cdm->obj[1].state_tca,
                                       -SB_CDM_BACKPROP_S);
    bool found;
    (void)memset(oc, 0, sizeof(*oc));
    found = sb_screen_orbits(a0, b0, cdm->obj[0].covariance,
                             cdm->obj[1].covariance, cdm->hbr_m,
                             SB_SCREEN_ARC_MAX_S, oc);
    *tca_epoch_gps_ms = (cdm->tca_gps_ms - (int64_t)(SB_CDM_BACKPROP_S * 1000.0)) +
                        (int64_t)(oc->conj.t_ca * 1000.0);
    return found;
}

const char *sb_cdm_status_str(SbCdmStatus st)
{
    switch (st)
    {
        case SB_CDM_OK:             return "ok";
        case SB_CDM_ERR_SYNTAX:     return "syntax error";
        case SB_CDM_ERR_NUMBER:     return "number outside exact-ingest grammar";
        case SB_CDM_ERR_UNITS:      return "unexpected units";
        case SB_CDM_ERR_MISSING:    return "mandatory field missing";
        case SB_CDM_ERR_DUPLICATE:  return "duplicate field";
        case SB_CDM_ERR_FRAME:      return "unsupported REF_FRAME";
        case SB_CDM_ERR_TIME:       return "invalid epoch";
        case SB_CDM_ERR_DOMAIN:     return "value outside SB/1 input domain";
        case SB_CDM_ERR_ID:         return "no usable object identity";
        case SB_CDM_ERR_SAME_OBJECT:return "both objects have the same identity";
        default:                    return "unknown";
    }
}
