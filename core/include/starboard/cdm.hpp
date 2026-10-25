// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — the CDM binding (SPEC §3.3a).
//
// CCSDS 508.0-B-1 Conjunction Data Message (KVN text) -> the canonical shared
// input: both objects' lattice states and inertial position covariances AT
// TCA, canonical identities, and the TCA epoch in GPS milliseconds.
//
// Two craft that receive the same CDM bytes MUST derive bit-identical shared
// inputs on any platform, so nothing here touches libc number parsing:
//
//  * EXACT DECIMAL INGEST. A numeric field is accepted only with <= 15
//    significant digits and a decimal exponent |e| <= 22 after folding in the
//    unit's power of ten (km -> m is e+3). Then the mantissa M < 10^15 < 2^53
//    and 10^|e| <= 10^22 are both exact binary64 values, and ONE IEEE multiply
//    or divide gives the correctly-rounded result (Clinger's fast path). Any
//    other field is rejected — fail-safe, never "best effort".
//  * EPOCHS by integer civil-calendar arithmetic and a pinned leap table.
//  * FRAMES: ITRF is mapped by the frozen-Earth rotation at TCA,
//    v_i = v_f + w x r_f, w = (0, 0, OMEGA_EARTH); GCRF/ICRF/EME2000/TEME
//    pass through.
//  * COVARIANCE: RTN -> inertial as C = B^T K B (B the Hill basis of the
//    canonical state, frames.hpp), pinned expansion order, then quantized.
//  * TCA ANCHORING: screening back-propagates both TCA states by
//    CDM_BACKPROP_S and searches forward over SCREEN_ARC_MAX_S. Because the
//    canonical integrator round-trips to well under a millimetre, the
//    encounter geometry both craft agree on is the PROVIDER's high-fidelity
//    TCA geometry — the J2-only canonical model only locates the TCA.
//
// This is an independent implementation of the same binding as
// ports/c/cdm.c; conformance/ext_bits.cpp cross-checks them bit for bit.
// Allocation-free, exception-free, recursion-free, bounded by input length.
// ============================================================================

#include "frames.hpp"
#include "id.hpp"
#include "screening.hpp"

#include <cstdint>
#include <cstring>

namespace starboard {
namespace cdm {

constexpr double OMEGA_EARTH_RADS = 7.292115e-5;   // IERS nominal, pinned
constexpr double CDM_BACKPROP_S = 512.0;           // TCA anchor -> t_ref
constexpr int MAX_SIG_DIGITS = 15;
constexpr uint32_t MAX_LINE = 512u;
constexpr int64_t GPS_UNIX_OFFSET_MS = 315964800000ll;

enum class Status : uint8_t {
    OK = 0, SYNTAX = 1, NUMBER = 2, UNITS = 3, MISSING = 4, DUPLICATE = 5,
    FRAME = 6, TIME = 7, DOMAIN = 8, ID = 9, SAME_OBJECT = 10
};

enum class Frame : uint8_t { GCRF = 0, TEME = 1, ITRF = 2 };
enum class Maneuverable : uint8_t { UNKNOWN = 0, YES = 1, NO = 2 };
enum class ObjectType : uint8_t {
    UNKNOWN = 0, PAYLOAD = 1, ROCKET_BODY = 2, DEBRIS = 3, OTHER = 4
};

struct Object {
    ObjectId     id;
    Frame        frame;
    Maneuverable maneuverable;
    ObjectType   object_type;
    CraftState   state_tca;     // canonical inertial state AT TCA
    Sym33        covariance;    // inertial position covariance, lattice
};

struct Message {
    int64_t tca_gps_ms;
    double  cdm_miss_m;
    double  hbr_m;
    Object  obj[2];
};

namespace detail {

struct Span {
    const char* p = nullptr;
    uint32_t n = 0u;

    bool eq(const char* lit) const {
        uint32_t i = 0u;
        for (; lit[i] != '\0'; ++i) {
            if ((i >= n) || (p[i] != lit[i])) { return false; }
        }
        return i == n;
    }
    bool ieq(const char* lit) const {
        uint32_t i = 0u;
        for (; lit[i] != '\0'; ++i) {
            if ((i >= n) || (upper(p[i]) != upper(lit[i]))) { return false; }
        }
        return i == n;
    }
    static char upper(char c) {
        return ((c >= 'a') && (c <= 'z')) ? static_cast<char>(c - 32) : c;
    }
};

inline bool is_space(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\f') ||
           (c == '\v');
}
inline bool is_digit(char c) { return (c >= '0') && (c <= '9'); }

inline Span trim(Span s) {
    while ((s.n > 0u) && is_space(s.p[0])) { ++s.p; --s.n; }
    while ((s.n > 0u) && is_space(s.p[s.n - 1u])) { --s.n; }
    return s;
}

constexpr double POW10[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

} // namespace detail

// Exact decimal ingest (SPEC §3.3a). See the header comment.
inline Status decimal_to_double(const char* s, uint32_t len, int scale_exp,
                                double* out) {
    *out = 0.0;
    uint32_t i = 0u;
    bool neg = false;
    if ((i < len) && ((s[i] == '+') || (s[i] == '-'))) {
        neg = (s[i] == '-');
        ++i;
    }
    uint64_t m = 0u;
    int sig = 0, pending = 0, frac_len = 0;
    bool any = false, started = false, in_frac = false;
    for (; i < len; ++i) {
        const char c = s[i];
        if (detail::is_digit(c)) {
            any = true;
            if (in_frac) { ++frac_len; }
            if (c == '0') {
                if (started) { ++pending; }
                continue;
            }
            for (; pending > 0; --pending) {           // flush inner zeros
                m *= 10u;
                if (++sig > MAX_SIG_DIGITS) { return Status::NUMBER; }
            }
            m = m * 10u + static_cast<uint64_t>(c - '0');
            started = true;
            if (++sig > MAX_SIG_DIGITS) { return Status::NUMBER; }
        } else if ((c == '.') && !in_frac) {
            in_frac = true;
        } else {
            break;
        }
    }
    if (!any) { return Status::NUMBER; }
    int32_t exp_val = 0;
    if ((i < len) && ((s[i] == 'e') || (s[i] == 'E'))) {
        ++i;
        bool eneg = false;
        if ((i < len) && ((s[i] == '+') || (s[i] == '-'))) {
            eneg = (s[i] == '-');
            ++i;
        }
        int nd = 0;
        for (; (i < len) && detail::is_digit(s[i]); ++i) {
            if (nd >= 4) { return Status::NUMBER; }
            exp_val = exp_val * 10 + (s[i] - '0');
            ++nd;
        }
        if (nd == 0) { return Status::NUMBER; }
        if (eneg) { exp_val = -exp_val; }
    }
    if (i != len) { return Status::NUMBER; }
    if (m == 0u) {
        *out = neg ? -0.0 : 0.0;
        return Status::OK;
    }
    const int32_t e10 = exp_val + pending - frac_len + scale_exp;
    if ((e10 > 22) || (e10 < -22)) { return Status::NUMBER; }
    double mag = static_cast<double>(m);           // exact
    mag = (e10 >= 0) ? (mag * detail::POW10[e10]) : (mag / detail::POW10[-e10]);
    *out = neg ? -mag : mag;
    return Status::OK;
}

namespace detail {

inline int64_t days_from_civil(int32_t y, int32_t m, int32_t d) {
    y -= (m <= 2) ? 1 : 0;
    const int32_t era = ((y >= 0) ? y : (y - 399)) / 400;
    const int32_t yoe = y - era * 400;
    const int32_t doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
    const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + doe - 719468;
}

inline int32_t days_in_month(int32_t y, int32_t m) {
    constexpr int32_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
    return ((m == 2) && leap) ? 29 : dim[m - 1];
}

// GPS - UTC steps (year, month): offset becomes index+1 s at 00:00 UTC on
// the 1st. Pinned through the 2017-01-01 leap second.
constexpr int32_t LEAP_YM[18][2] = {
    {1981, 7}, {1982, 7}, {1983, 7}, {1985, 7}, {1988, 1}, {1990, 1},
    {1991, 1}, {1992, 7}, {1993, 7}, {1994, 7}, {1996, 1}, {1997, 7},
    {1999, 1}, {2006, 1}, {2009, 1}, {2012, 7}, {2015, 7}, {2017, 1}};

} // namespace detail

inline Status utc_to_gps_ms(int32_t y, int32_t mo, int32_t d, int32_t h,
                            int32_t mi, int32_t s, int32_t ms, int64_t* out) {
    *out = 0;
    if ((y < 1980) || (y > 2200) || (mo < 1) || (mo > 12) || (d < 1) ||
        (d > detail::days_in_month(y, mo)) || (h < 0) || (h > 23) ||
        (mi < 0) || (mi > 59) || (s < 0) || (s > 59) || (ms < 0) ||
        (ms > 1000)) {
        return Status::TIME;
    }
    const int64_t unix_ms =
        ((((detail::days_from_civil(y, mo, d) * 24) + h) * 60 + mi) * 60 + s) *
            1000 + ms;
    int64_t leap = 0;
    for (int k = 0; k < 18; ++k) {
        if (unix_ms >= detail::days_from_civil(detail::LEAP_YM[k][0],
                                               detail::LEAP_YM[k][1], 1) *
                           86400000ll) {
            leap = k + 1;
        }
    }
    *out = unix_ms - GPS_UNIX_OFFSET_MS + leap * 1000;
    if (*out < 0) {
        *out = 0;
        return Status::TIME;
    }
    return Status::OK;
}

namespace detail {

inline bool parse_uint(const char* p, uint32_t n, int32_t* out) {
    if ((n == 0u) || (n > 6u)) { return false; }
    int32_t v = 0;
    for (uint32_t i = 0u; i < n; ++i) {
        if (!is_digit(p[i])) { return false; }
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return true;
}

// "YYYY-MM-DDThh:mm:ss[.f+][Z]" or "YYYY-DDDThh:mm:ss[.f+][Z]"; fractional
// seconds round half-to-even to the millisecond by exact digit comparison.
inline Status parse_epoch(Span v, int64_t* gps_ms) {
    const char* p = v.p;
    uint32_t n = v.n;
    if ((n > 0u) && ((p[n - 1u] == 'Z') || (p[n - 1u] == 'z'))) { --n; }
    if ((n < 17u) || (p[4] != '-')) { return Status::TIME; }
    int32_t y, mo, d, hh, mi, ss, ms = 0;
    if (!parse_uint(p, 4u, &y)) { return Status::TIME; }
    uint32_t t_at;
    if ((n >= 19u) && (p[7] == '-') && (p[10] == 'T')) {
        if (!parse_uint(&p[5], 2u, &mo) || !parse_uint(&p[8], 2u, &d)) {
            return Status::TIME;
        }
        t_at = 10u;
    } else if (p[8] == 'T') {
        int32_t doy;
        if (!parse_uint(&p[5], 3u, &doy) || (y < 1980) || (y > 2200)) {
            return Status::TIME;
        }
        mo = 1;
        while ((mo <= 12) && (doy > days_in_month(y, mo))) {
            doy -= days_in_month(y, mo);
            ++mo;
        }
        if ((doy < 1) || (mo > 12)) { return Status::TIME; }
        d = doy;
        t_at = 8u;
    } else {
        return Status::TIME;
    }
    if ((n < t_at + 9u) || (p[t_at + 3u] != ':') || (p[t_at + 6u] != ':') ||
        !parse_uint(&p[t_at + 1u], 2u, &hh) ||
        !parse_uint(&p[t_at + 4u], 2u, &mi) ||
        !parse_uint(&p[t_at + 7u], 2u, &ss)) {
        return Status::TIME;
    }
    uint32_t i = t_at + 9u;
    if (i < n) {
        if ((p[i] != '.') || (i + 1u >= n)) { return Status::TIME; }
        uint32_t nd = 0u;
        int32_t first_rest = -1;
        bool rest_nonzero = false;
        for (++i; i < n; ++i, ++nd) {
            if (!is_digit(p[i])) { return Status::TIME; }
            const int32_t dg = p[i] - '0';
            if (nd < 3u) { ms = ms * 10 + dg; }
            else if (nd == 3u) { first_rest = dg; }
            else if (dg != 0) { rest_nonzero = true; }
        }
        for (; nd < 3u; ++nd) { ms *= 10; }
        if ((first_rest > 5) || ((first_rest == 5) && rest_nonzero) ||
            ((first_rest == 5) && !rest_nonzero && (ms % 2 == 1))) {
            ++ms;
        }
    }
    return utc_to_gps_ms(y, mo, d, hh, mi, ss, ms, gps_ms);
}

// Field tables. Units: the CDM unit and its power of ten to SI.
struct FieldDef {
    const char* key;
    const char* unit;   // nullptr: text field
    int scale_exp;
    bool required;
};

constexpr int HDR_N = 7;
constexpr FieldDef HDR[HDR_N] = {
    {"CCSDS_CDM_VERS", nullptr, 0, true}, {"MESSAGE_ID", nullptr, 0, true},
    {"ORIGINATOR", nullptr, 0, true},     {"TCA", nullptr, 0, true},
    {"MISS_DISTANCE", "m", 0, true},
    {"COLLISION_PROBABILITY", "", 0, false}, {"HBR", "m", 0, false}};
enum { H_VERS, H_MSGID, H_ORIG, H_TCA, H_MISS, H_PC, H_HBR };

constexpr int OBJ_N = 18;
constexpr FieldDef OBJ[OBJ_N] = {
    {"OBJECT_DESIGNATOR", nullptr, 0, true},
    {"OBJECT_NAME", nullptr, 0, false},
    {"INTERNATIONAL_DESIGNATOR", nullptr, 0, true},
    {"OBJECT_TYPE", nullptr, 0, false},
    {"MANEUVERABLE", nullptr, 0, true},
    {"REF_FRAME", nullptr, 0, true},
    {"X", "km", 3, true},       {"Y", "km", 3, true},
    {"Z", "km", 3, true},       {"X_DOT", "km/s", 3, true},
    {"Y_DOT", "km/s", 3, true}, {"Z_DOT", "km/s", 3, true},
    {"CR_R", "m**2", 0, true},  {"CT_R", "m**2", 0, true},
    {"CT_T", "m**2", 0, true},  {"CN_R", "m**2", 0, true},
    {"CN_T", "m**2", 0, true},  {"CN_N", "m**2", 0, true}};
enum { O_DESIG, O_NAME, O_INTL, O_TYPE, O_MAN, O_FRAME, O_X, O_Y, O_Z,
       O_XD, O_YD, O_ZD, O_CRR, O_CTR, O_CTT, O_CNR, O_CNT, O_CNN };

struct Fields {
    double num[OBJ_N] = {};
    Span txt[OBJ_N];
    uint32_t seen = 0u;
};

inline int lookup(Span key, const FieldDef* defs, int n) {
    for (int i = 0; i < n; ++i) {
        if (key.eq(defs[i].key)) { return i; }
    }
    return -1;
}

inline Status make_identity(const Fields& f, Object* o) {
    char buf[13];
    const Span src = f.txt[O_INTL];
    uint32_t len = 0u;
    if ((src.n == 0u) || src.ieq("UNKNOWN")) {
        const Span d = f.txt[O_DESIG];
        if ((d.n == 0u) || (d.n > 9u)) { return Status::ID; }
        buf[0] = 'C'; buf[1] = 'A'; buf[2] = 'T';
        for (uint32_t i = 0u; i < d.n; ++i) { buf[3u + i] = d.p[i]; }
        len = 3u + d.n;
    } else {
        if (src.n > 12u) { return Status::ID; }
        for (uint32_t i = 0u; i < src.n; ++i) { buf[i] = src.p[i]; }
        len = src.n;
    }
    buf[len] = '\0';
    for (uint32_t i = 0u; i < len; ++i) {
        const char c = Span::upper(buf[i]);
        if (!(is_digit(c) || ((c >= 'A') && (c <= 'Z')) || (c == '-'))) {
            return Status::ID;
        }
    }
    o->id = make_object_id(buf);
    return Status::OK;
}

// C = B^T K B, pinned: M = K B (inner index ascending), then C = B^T M.
inline Sym33 rtn_to_inertial(const Mat3& b, const double k[3][3]) {
    double mk[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int j = 0; j < 3; ++j) {
            mk[r][j] = (k[r][0] * b.m[0][j] + k[r][1] * b.m[1][j]) +
                       k[r][2] * b.m[2][j];
        }
    }
    double c[3][3] = {};
    for (int r = 0; r < 3; ++r) {
        for (int j = r; j < 3; ++j) {
            c[r][j] = (b.m[0][r] * mk[0][j] + b.m[1][r] * mk[1][j]) +
                      b.m[2][r] * mk[2][j];
        }
    }
    return cov_quantize(Sym33{c[0][0], c[0][1], c[0][2], c[1][1], c[1][2],
                              c[2][2]});
}

inline Status bind_object(const Fields& f, Object* o) {
    const Status s = make_identity(f, o);
    if (s != Status::OK) { return s; }
    const Span fr = f.txt[O_FRAME];
    if (fr.eq("GCRF") || fr.eq("ICRF") || fr.eq("EME2000")) {
        o->frame = Frame::GCRF;
    } else if (fr.eq("TEME")) {
        o->frame = Frame::TEME;
    } else if (fr.eq("ITRF") || fr.eq("ITRF2000") || fr.eq("ITRF-93") ||
               fr.eq("ITRF-97") || fr.eq("ITRF2008") || fr.eq("ITRF2014") ||
               fr.eq("ITRF2020")) {
        o->frame = Frame::ITRF;
    } else {
        return Status::FRAME;
    }
    const Span mv = f.txt[O_MAN];
    if (mv.eq("YES")) { o->maneuverable = Maneuverable::YES; }
    else if (mv.eq("NO")) { o->maneuverable = Maneuverable::NO; }
    else if (mv.eq("N/A")) { o->maneuverable = Maneuverable::UNKNOWN; }
    else { return Status::SYNTAX; }
    const Span ty = f.txt[O_TYPE];
    o->object_type = ty.eq("PAYLOAD")       ? ObjectType::PAYLOAD
                   : ty.eq("ROCKET BODY")   ? ObjectType::ROCKET_BODY
                   : ty.eq("DEBRIS")        ? ObjectType::DEBRIS
                   : ty.eq("OTHER")         ? ObjectType::OTHER
                                            : ObjectType::UNKNOWN;
    CraftState st;
    st.position = Vec3(f.num[O_X], f.num[O_Y], f.num[O_Z]);
    st.velocity = Vec3(f.num[O_XD], f.num[O_YD], f.num[O_ZD]);
    if (o->frame == Frame::ITRF) {
        // Frozen-Earth rotation at TCA: v_i = v_f + w x r_f.
        st.velocity.x = st.velocity.x - OMEGA_EARTH_RADS * st.position.y;
        st.velocity.y = st.velocity.y + OMEGA_EARTH_RADS * st.position.x;
    }
    o->state_tca = canonicalize(st);
    const double rn = o->state_tca.position.norm();
    const double vn = o->state_tca.velocity.norm();
    if ((rn < 6.0e6) || (rn > 5.0e7) || (vn < 1.0e3) || (vn > 2.0e4)) {
        return Status::DOMAIN;
    }
    const int diag[3] = {O_CRR, O_CTT, O_CNN};
    for (int k : diag) {
        if ((f.num[k] < 0.0) || (f.num[k] > 1.0e14)) { return Status::DOMAIN; }
    }
    const double k[3][3] = {{f.num[O_CRR], f.num[O_CTR], f.num[O_CNR]},
                            {f.num[O_CTR], f.num[O_CTT], f.num[O_CNT]},
                            {f.num[O_CNR], f.num[O_CNT], f.num[O_CNN]}};
    o->covariance = rtn_to_inertial(hill_basis(o->state_tca), k);
    return Status::OK;
}

} // namespace detail

// Parse a KVN CDM of `len` bytes into the canonical shared input.
inline Status parse(const char* text, uint32_t len, Message* out,
                    uint32_t* err_line = nullptr) {
    using namespace detail;
    *out = Message();
    if (err_line != nullptr) { *err_line = 0u; }
    Fields of[2];
    double hnum[HDR_N] = {};
    uint32_t hseen = 0u;
    int section = 0;
    uint32_t pos = 0u, line_no = 0u;
    Status st = Status::OK;

    while ((pos < len) && (st == Status::OK)) {
        uint32_t end = pos;
        while ((end < len) && (text[end] != '\n')) { ++end; }
        ++line_no;
        Span line{&text[pos], end - pos};
        pos = (end < len) ? end + 1u : end;
        if (line.n > MAX_LINE) { st = Status::SYNTAX; break; }
        line = trim(line);
        if (line.n == 0u) { continue; }
        if ((line.n >= 7u) && (std::memcmp(line.p, "COMMENT", 7u) == 0) &&
            ((line.n == 7u) || is_space(line.p[7]))) {
            continue;
        }
        uint32_t eq = 0u;
        while ((eq < line.n) && (line.p[eq] != '=')) { ++eq; }
        if (eq >= line.n) { st = Status::SYNTAX; break; }
        const Span key = trim(Span{line.p, eq});
        Span val = trim(Span{&line.p[eq + 1u], line.n - eq - 1u});
        Span unit{val.p, 0u};
        if ((val.n > 0u) && (val.p[val.n - 1u] == ']')) {
            uint32_t ob = val.n - 1u;
            while ((ob > 0u) && (val.p[ob] != '[')) { --ob; }
            if (val.p[ob] != '[') { st = Status::SYNTAX; break; }
            unit = trim(Span{&val.p[ob + 1u], val.n - ob - 2u});
            val = trim(Span{val.p, ob});
        }
        if ((key.n == 0u) || (val.n == 0u)) { st = Status::SYNTAX; break; }
        if (key.eq("OBJECT")) {
            if (val.eq("OBJECT1") && (section == 0)) { section = 1; }
            else if (val.eq("OBJECT2") && (section == 1)) { section = 2; }
            else { st = Status::SYNTAX; }
            continue;
        }
        const FieldDef* defs = (section == 0) ? HDR : OBJ;
        const int n_defs = (section == 0) ? HDR_N : OBJ_N;
        const int f = lookup(key, defs, n_defs);
        if (f < 0) { continue; }                      // informative field
        uint32_t& seen = (section == 0) ? hseen : of[section - 1].seen;
        if ((seen & (1u << f)) != 0u) { st = Status::DUPLICATE; break; }
        seen |= (1u << f);
        const FieldDef& def = defs[f];
        if (section == 0) {
            if (f == H_TCA) {
                st = parse_epoch(val, &out->tca_gps_ms);
            } else if (def.unit != nullptr) {
                if ((unit.n != 0u) && ((def.unit[0] == '\0') || !unit.ieq(def.unit))) {
                    st = Status::UNITS;
                    break;
                }
                st = decimal_to_double(val.p, val.n, def.scale_exp, &hnum[f]);
            }
        } else {
            Fields& fo = of[section - 1];
            fo.txt[f] = val;
            if (def.unit != nullptr) {
                if ((unit.n != 0u) && !unit.ieq(def.unit)) {
                    st = Status::UNITS;
                    break;
                }
                st = decimal_to_double(val.p, val.n, def.scale_exp, &fo.num[f]);
            } else if (unit.n != 0u) {
                st = Status::UNITS;
            }
        }
    }

    if (st == Status::OK) {
        uint32_t hreq = 0u, oreq = 0u;
        for (int i = 0; i < HDR_N; ++i) { if (HDR[i].required) { hreq |= 1u << i; } }
        for (int i = 0; i < OBJ_N; ++i) { if (OBJ[i].required) { oreq |= 1u << i; } }
        if (((hseen & hreq) != hreq) || (section != 2) ||
            ((of[0].seen & oreq) != oreq) || ((of[1].seen & oreq) != oreq)) {
            st = Status::MISSING;
            line_no = 0u;
        }
    }
    if (st == Status::OK) {
        out->cdm_miss_m = hnum[H_MISS];
        out->hbr_m = ((hseen & (1u << H_HBR)) != 0u) ? hnum[H_HBR]
                                                     : 2.0 * canon::DEFAULT_HBR_M;
        if (!(out->hbr_m > 0.0) || (out->hbr_m > 1.0e3) ||
            !(out->cdm_miss_m >= 0.0)) {
            st = Status::DOMAIN;
        }
        for (int i = 0; (i < 2) && (st == Status::OK); ++i) {
            st = bind_object(of[i], &out->obj[i]);
        }
        line_no = 0u;
    }
    if ((st == Status::OK) && id_equal(out->obj[0].id, out->obj[1].id)) {
        st = Status::SAME_OBJECT;
    }
    if (st != Status::OK) {
        *out = Message();
        if (err_line != nullptr) { *err_line = line_no; }
    }
    return st;
}

// The binding's screening step: anchor at the CDM TCA, back-propagate both
// canonical states by CDM_BACKPROP_S, run the canonical search over the full
// arc. Returns detection; fills *oc and the canonical TCA epoch (GPS ms).
inline bool screen(const Message& m, OrbitConjunction* oc,
                   int64_t* tca_epoch_gps_ms) {
    const CraftState a0 = propagate(m.obj[0].state_tca, -CDM_BACKPROP_S);
    const CraftState b0 = propagate(m.obj[1].state_tca, -CDM_BACKPROP_S);
    *oc = OrbitConjunction();
    const bool found = screen_orbits(a0, b0, m.obj[0].covariance,
                                     m.obj[1].covariance, m.hbr_m,
                                     canon::SCREEN_ARC_MAX_S, oc);
    *tca_epoch_gps_ms = (m.tca_gps_ms -
                         static_cast<int64_t>(CDM_BACKPROP_S * 1000.0)) +
                        static_cast<int64_t>(oc->conj.t_ca * 1000.0);
    return found;
}

} // namespace cdm
} // namespace starboard
