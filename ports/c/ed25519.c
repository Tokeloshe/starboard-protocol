// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// ed25519.c — RFC 8032 Ed25519 signature VERIFICATION, portable C99.
//
// Port of core/include/starboard/crypto/ed25519_verify.hpp with IDENTICAL
// acceptance semantics (any input accepted by one is accepted by the other;
// pinned by ports/c/tests/diff_ed25519.cpp and the Wycheproof driver):
//   - cofactorless: R' = [S]B - [k]A, canonical encoding compared to R bytes;
//   - S >= L rejected (malleability);
//   - public key: all-zero rejected; non-canonical y (>= p) rejected;
//     off-curve rejected; x == 0 with the sign bit set rejected;
//     small-order keys ([8]A == identity) rejected.
//
// Target profile: 32-bit flight MCUs (e.g. SPARC LEON3) with NO 128-bit
// integer type. The field representation is therefore the ref10-style
// 10-limb radix-2^25.5 form (limb i holds bits [pos_i, pos_i + w_i) with
// w = 26,25,26,25,...): int32_t limbs, and the ONLY multiplications are
// int32_t x int32_t -> int64_t (a single 32x32->64 multiply instruction on a
// 32-bit core). No __int128, no 64x64 products.
//
// Limb invariant ("tight"): every routine that produces an sb_fe leaves
// limbs 1..9 in [0, 2^w_i) except limb 1 in (-2^14, 2^25 + 2^14), and limb 0
// in [0, 2^26). Hence |limb| <= 2^26 for every input to sb_fe_mul, which
// bounds 2*f_i and 19*g_j below 2^31 and every column sum below 2^61.
//
// Verify-only: every input is public, so variable-time code is acceptable
// (no secret for a timing channel to leak). No allocation, no recursion, no
// VLAs; braces everywhere; explicit-width types.
//
// MISRA notes: right shifts of negative signed values (implementation-
// defined in C99) and left shifts of negative values (undefined) are
// avoided entirely: carries use sb_floor_shift (exact floor division by a
// power of two, defined for every int64_t > INT64_MIN) and multiplication
// by 2^w instead of <<.

#include "starboard_registry.h"

// ---------------------------------------------------------------------------
// Field arithmetic mod p = 2^255 - 19.
// ---------------------------------------------------------------------------

typedef struct {
    int32_t v[10];
} sb_fe;

static const uint32_t SB_FE_POS[10] = {
    0U, 26U, 51U, 77U, 102U, 128U, 153U, 179U, 204U, 230U
};
static const uint32_t SB_FE_WIDTH[10] = {
    26U, 25U, 26U, 25U, 26U, 25U, 26U, 25U, 26U, 25U
};

// d = -121665/121666 mod p (canonical limbs, generated from the integer).
static const sb_fe SB_FE_D = { {
    56195235, 13857412, 51736253, 6949390, 114729,
    24766616, 60832955, 30306712, 48412415, 21499315
} };
// 2*d mod p
static const sb_fe SB_FE_D2 = { {
    45281625, 27714825, 36363642, 13898781, 229458,
    15978800, 54557047, 27058993, 29715967, 9444199
} };
// sqrt(-1) = 2^((p-1)/4) mod p
static const sb_fe SB_FE_SQRTM1 = { {
    34513072, 25610706, 9377949, 3500415, 12389472,
    33281959, 41962654, 31548777, 326685, 11406482
} };

// floor(x / 2^n) for 0 < n < 63, without shifting a negative value.
static int64_t sb_floor_shift(int64_t x, uint32_t n)
{
    int64_t r;
    if (x >= 0)
    {
        r = (int64_t)((uint64_t)x >> n);
    }
    else
    {
        // floor(x / 2^n) = -(((-x) - 1) / 2^n) - 1 for x < 0.
        const uint64_t m = (uint64_t)(-(x + 1));
        r = -(int64_t)(m >> n) - 1;
    }
    return r;
}

// Reduce a wide (int64) limb vector to a tight sb_fe: one sequential floor
// carry pass 0 -> 9, wrap the top carry with weight 19 (2^255 = 19 mod p),
// then one more carry 0 -> 1.
static void sb_fe_carry_wide(sb_fe *r, int64_t h[10])
{
    uint32_t i;
    int64_t c;
    for (i = 0U; i < 9U; ++i)
    {
        c = sb_floor_shift(h[i], SB_FE_WIDTH[i]);
        h[i] -= c * ((int64_t)1 << SB_FE_WIDTH[i]);
        h[i + 1U] += c;
    }
    c = sb_floor_shift(h[9], 25U);
    h[9] -= c * ((int64_t)1 << 25);
    h[0] += c * 19;
    c = sb_floor_shift(h[0], 26U);
    h[0] -= c * ((int64_t)1 << 26);
    h[1] += c;
    for (i = 0U; i < 10U; ++i)
    {
        r->v[i] = (int32_t)h[i];
    }
}

static void sb_fe_zero(sb_fe *r)
{
    uint32_t i;
    for (i = 0U; i < 10U; ++i)
    {
        r->v[i] = 0;
    }
}

static void sb_fe_one(sb_fe *r)
{
    sb_fe_zero(r);
    r->v[0] = 1;
}

static void sb_fe_copy(sb_fe *r, const sb_fe *a)
{
    uint32_t i;
    for (i = 0U; i < 10U; ++i)
    {
        r->v[i] = a->v[i];
    }
}

static void sb_fe_add(sb_fe *r, const sb_fe *a, const sb_fe *b)
{
    int64_t h[10];
    uint32_t i;
    for (i = 0U; i < 10U; ++i)
    {
        h[i] = (int64_t)a->v[i] + (int64_t)b->v[i];
    }
    sb_fe_carry_wide(r, h);
}

static void sb_fe_sub(sb_fe *r, const sb_fe *a, const sb_fe *b)
{
    int64_t h[10];
    uint32_t i;
    for (i = 0U; i < 10U; ++i)
    {
        h[i] = (int64_t)a->v[i] - (int64_t)b->v[i];
    }
    sb_fe_carry_wide(r, h);
}

static void sb_fe_neg(sb_fe *r, const sb_fe *a)
{
    sb_fe zero;
    sb_fe_zero(&zero);
    sb_fe_sub(r, &zero, a);
}

// Schoolbook 10x10 product. Column k = i + j; when both i and j are odd the
// limb weights sum to one more bit than the column weight (factor 2); when
// k >= 10 the term wraps to column k - 10 with factor 19 (2^255 = 19 mod p).
// Every product is int32_t x int32_t -> int64_t. Safe for r aliasing a/b.
static void sb_fe_mul(sb_fe *r, const sb_fe *a, const sb_fe *b)
{
    int64_t h[10];
    int32_t g19[10];
    uint32_t i;
    uint32_t j;
    for (i = 0U; i < 10U; ++i)
    {
        h[i] = 0;
        g19[i] = 19 * b->v[i];          // |b_i| <= 2^26: fits int32
    }
    for (i = 0U; i < 10U; ++i)
    {
        const int32_t fi = a->v[i];
        const int32_t fi2 = 2 * fi;     // |a_i| <= 2^26: fits int32
        for (j = 0U; j < 10U; ++j)
        {
            const bool both_odd = ((i & 1U) != 0U) && ((j & 1U) != 0U);
            const int32_t f = both_odd ? fi2 : fi;
            if ((i + j) < 10U)
            {
                h[i + j] += (int64_t)f * (int64_t)b->v[j];
            }
            else
            {
                h[(i + j) - 10U] += (int64_t)f * (int64_t)g19[j];
            }
        }
    }
    sb_fe_carry_wide(r, h);
}

static void sb_fe_sq(sb_fe *r, const sb_fe *a)
{
    sb_fe_mul(r, a, a);
}

static void sb_fe_sq_n(sb_fe *r, const sb_fe *a, uint32_t n)
{
    uint32_t i;
    sb_fe_copy(r, a);
    for (i = 0U; i < n; ++i)
    {
        sb_fe_sq(r, r);
    }
}

// Fully reduce and serialize to 32 little-endian bytes (canonical, < p).
static void sb_fe_tobytes(uint8_t out[32], const sb_fe *a)
{
    int64_t h[10];
    uint32_t i;
    uint32_t pass;
    bool ge_p;

    for (i = 0U; i < 10U; ++i)
    {
        h[i] = (int64_t)a->v[i];
    }
    // Three floor-carry passes bring every limb into [0, 2^w_i), i.e. the
    // value into [0, 2^255) (see the bound argument in the file header:
    // after pass 1 the top carry is tiny; pass 2 can only borrow/carry by
    // one; pass 3 settles limb 0).
    for (pass = 0U; pass < 3U; ++pass)
    {
        int64_t c;
        for (i = 0U; i < 9U; ++i)
        {
            c = sb_floor_shift(h[i], SB_FE_WIDTH[i]);
            h[i] -= c * ((int64_t)1 << SB_FE_WIDTH[i]);
            h[i + 1U] += c;
        }
        c = sb_floor_shift(h[9], 25U);
        h[9] -= c * ((int64_t)1 << 25);
        h[0] += c * 19;
    }
    {
        // Final settle of limb 0 (no wrap can occur from here).
        const int64_t c = sb_floor_shift(h[0], 26U);
        h[0] -= c * ((int64_t)1 << 26);
        h[1] += c;
    }
    // Value in [0, 2^255). It is >= p iff limb 0 >= 2^26 - 19 and every
    // other limb is all-ones; then subtract p.
    ge_p = (h[0] >= (((int64_t)1 << 26) - 19));
    for (i = 1U; i < 10U; ++i)
    {
        if (h[i] != (((int64_t)1 << SB_FE_WIDTH[i]) - 1))
        {
            ge_p = false;
        }
    }
    if (ge_p)
    {
        h[0] -= ((int64_t)1 << 26) - 19;
        for (i = 1U; i < 10U; ++i)
        {
            h[i] = 0;
        }
    }
    for (i = 0U; i < 32U; ++i)
    {
        out[i] = 0U;
    }
    for (i = 0U; i < 10U; ++i)
    {
        uint32_t b;
        const uint32_t limb = (uint32_t)h[i];
        for (b = 0U; b < SB_FE_WIDTH[i]; ++b)
        {
            if (((limb >> b) & 1U) != 0U)
            {
                const uint32_t bit = SB_FE_POS[i] + b;
                out[bit >> 3] = (uint8_t)(out[bit >> 3] | (uint8_t)(1U << (bit & 7U)));
            }
        }
    }
}

// Deserialize 32 little-endian bytes; bit 255 is ignored. Does NOT check
// canonicity (callers check the raw bytes against p first).
static void sb_fe_frombytes(sb_fe *r, const uint8_t in[32])
{
    uint32_t i;
    for (i = 0U; i < 10U; ++i)
    {
        uint32_t b;
        uint32_t limb = 0U;
        for (b = 0U; b < SB_FE_WIDTH[i]; ++b)
        {
            const uint32_t bit = SB_FE_POS[i] + b;   // <= 254
            if (((uint32_t)(in[bit >> 3] >> (bit & 7U)) & 1U) != 0U)
            {
                limb |= (1U << b);
            }
        }
        r->v[i] = (int32_t)limb;
    }
}

static bool sb_fe_iszero(const sb_fe *a)
{
    uint8_t b[32];
    uint8_t acc = 0U;
    uint32_t i;
    sb_fe_tobytes(b, a);
    for (i = 0U; i < 32U; ++i)
    {
        acc = (uint8_t)(acc | b[i]);
    }
    return acc == 0U;
}

static bool sb_fe_eq(const sb_fe *a, const sb_fe *b)
{
    uint8_t ba[32];
    uint8_t bb[32];
    uint8_t acc = 0U;
    uint32_t i;
    sb_fe_tobytes(ba, a);
    sb_fe_tobytes(bb, b);
    for (i = 0U; i < 32U; ++i)
    {
        acc = (uint8_t)(acc | (uint8_t)(ba[i] ^ bb[i]));
    }
    return acc == 0U;
}

// Least significant bit of the canonical representation (RFC 8032 sign).
static bool sb_fe_isnegative(const sb_fe *a)
{
    uint8_t b[32];
    sb_fe_tobytes(b, a);
    return (b[0] & 1U) != 0U;
}

// r = z^(2^252 - 3). Same fixed addition chain as the C++ reference.
static void sb_fe_pow22523(sb_fe *r, const sb_fe *z)
{
    sb_fe t0;
    sb_fe t1;
    sb_fe t2;
    sb_fe_sq(&t0, z);                 // z^2
    sb_fe_sq_n(&t1, &t0, 2U);         // z^8
    sb_fe_mul(&t1, z, &t1);           // z^9
    sb_fe_mul(&t0, &t0, &t1);         // z^11
    sb_fe_sq(&t0, &t0);               // z^22
    sb_fe_mul(&t0, &t1, &t0);         // z^(2^5 - 1)
    sb_fe_sq_n(&t1, &t0, 5U);
    sb_fe_mul(&t0, &t1, &t0);         // z^(2^10 - 1)
    sb_fe_sq_n(&t1, &t0, 10U);
    sb_fe_mul(&t1, &t1, &t0);         // z^(2^20 - 1)
    sb_fe_sq_n(&t2, &t1, 20U);
    sb_fe_mul(&t1, &t2, &t1);         // z^(2^40 - 1)
    sb_fe_sq_n(&t1, &t1, 10U);
    sb_fe_mul(&t0, &t1, &t0);         // z^(2^50 - 1)
    sb_fe_sq_n(&t1, &t0, 50U);
    sb_fe_mul(&t1, &t1, &t0);         // z^(2^100 - 1)
    sb_fe_sq_n(&t2, &t1, 100U);
    sb_fe_mul(&t1, &t2, &t1);         // z^(2^200 - 1)
    sb_fe_sq_n(&t1, &t1, 50U);
    sb_fe_mul(&t0, &t1, &t0);         // z^(2^250 - 1)
    sb_fe_sq_n(&t0, &t0, 2U);         // z^(2^252 - 4)
    sb_fe_mul(r, &t0, z);             // z^(2^252 - 3)
}

// r = z^(p-2) = z^(2^255 - 21) (inverse for z != 0). Same chain as C++.
static void sb_fe_invert(sb_fe *r, const sb_fe *z)
{
    sb_fe t0;
    sb_fe t1;
    sb_fe t2;
    sb_fe t3;
    sb_fe_sq(&t0, z);                 // z^2
    sb_fe_sq_n(&t1, &t0, 2U);         // z^8
    sb_fe_mul(&t1, z, &t1);           // z^9
    sb_fe_mul(&t0, &t0, &t1);         // z^11
    sb_fe_sq(&t2, &t0);               // z^22
    sb_fe_mul(&t1, &t1, &t2);         // z^(2^5 - 1)
    sb_fe_sq_n(&t2, &t1, 5U);
    sb_fe_mul(&t1, &t2, &t1);         // z^(2^10 - 1)
    sb_fe_sq_n(&t2, &t1, 10U);
    sb_fe_mul(&t2, &t2, &t1);         // z^(2^20 - 1)
    sb_fe_sq_n(&t3, &t2, 20U);
    sb_fe_mul(&t2, &t3, &t2);         // z^(2^40 - 1)
    sb_fe_sq_n(&t2, &t2, 10U);
    sb_fe_mul(&t1, &t2, &t1);         // z^(2^50 - 1)
    sb_fe_sq_n(&t2, &t1, 50U);
    sb_fe_mul(&t2, &t2, &t1);         // z^(2^100 - 1)
    sb_fe_sq_n(&t3, &t2, 100U);
    sb_fe_mul(&t2, &t3, &t2);         // z^(2^200 - 1)
    sb_fe_sq_n(&t2, &t2, 50U);
    sb_fe_mul(&t1, &t2, &t1);         // z^(2^250 - 1)
    sb_fe_sq_n(&t1, &t1, 5U);         // z^(2^255 - 32)
    sb_fe_mul(r, &t1, &t0);           // z^(2^255 - 21)
}

// ---------------------------------------------------------------------------
// Group: extended twisted-Edwards coordinates (X:Y:Z:T), a = -1.
// ---------------------------------------------------------------------------

typedef struct {
    sb_fe x;
    sb_fe y;
    sb_fe z;
    sb_fe t;
} sb_ge;

static void sb_ge_identity(sb_ge *r)
{
    sb_fe_zero(&r->x);
    sb_fe_one(&r->y);
    sb_fe_one(&r->z);
    sb_fe_zero(&r->t);
}

static void sb_ge_copy(sb_ge *r, const sb_ge *a)
{
    sb_fe_copy(&r->x, &a->x);
    sb_fe_copy(&r->y, &a->y);
    sb_fe_copy(&r->z, &a->z);
    sb_fe_copy(&r->t, &a->t);
}

static void sb_ge_neg(sb_ge *r, const sb_ge *a)
{
    sb_fe_neg(&r->x, &a->x);
    sb_fe_copy(&r->y, &a->y);
    sb_fe_copy(&r->z, &a->z);
    sb_fe_neg(&r->t, &a->t);
}

// Unified/complete addition add-2008-hwcd-3 (also used for doubling).
// Safe for r aliasing p or q (all reads complete before r is written).
static void sb_ge_add(sb_ge *r, const sb_ge *p, const sb_ge *q)
{
    sb_fe a;
    sb_fe b;
    sb_fe c;
    sb_fe dd;
    sb_fe e;
    sb_fe f;
    sb_fe g;
    sb_fe h;
    sb_fe t0;
    sb_fe t1;
    sb_fe_sub(&t0, &p->y, &p->x);
    sb_fe_sub(&t1, &q->y, &q->x);
    sb_fe_mul(&a, &t0, &t1);          // A = (Y1-X1)*(Y2-X2)
    sb_fe_add(&t0, &p->y, &p->x);
    sb_fe_add(&t1, &q->y, &q->x);
    sb_fe_mul(&b, &t0, &t1);          // B = (Y1+X1)*(Y2+X2)
    sb_fe_mul(&c, &p->t, &q->t);
    sb_fe_mul(&c, &c, &SB_FE_D2);     // C = 2d*T1*T2
    sb_fe_mul(&dd, &p->z, &q->z);
    sb_fe_add(&dd, &dd, &dd);         // D = 2*Z1*Z2
    sb_fe_sub(&e, &b, &a);            // E = B - A
    sb_fe_sub(&f, &dd, &c);           // F = D - C
    sb_fe_add(&g, &dd, &c);           // G = D + C
    sb_fe_add(&h, &b, &a);            // H = B + A
    sb_fe_mul(&r->x, &e, &f);
    sb_fe_mul(&r->y, &g, &h);
    sb_fe_mul(&r->t, &e, &h);
    sb_fe_mul(&r->z, &f, &g);
}

static bool sb_ge_is_identity(const sb_ge *p)
{
    return sb_fe_iszero(&p->x) && sb_fe_eq(&p->y, &p->z);
}

// True iff [8]P is the identity (P in the torsion subgroup).
static bool sb_ge_is_small_order(const sb_ge *p)
{
    sb_ge t;
    sb_ge_add(&t, p, p);
    sb_ge_add(&t, &t, &t);
    sb_ge_add(&t, &t, &t);
    return sb_ge_is_identity(&t);
}

// Variable-time MSB-first double-and-add; scalar is 32 LE bytes.
static void sb_ge_scalarmul(sb_ge *r, const uint8_t scalar[32], const sb_ge *p)
{
    sb_ge acc;
    uint32_t k;
    sb_ge_identity(&acc);
    for (k = 0U; k < 256U; ++k)
    {
        const uint32_t i = 255U - k;
        const uint32_t bit = ((uint32_t)scalar[i >> 3] >> (i & 7U)) & 1U;
        sb_ge_add(&acc, &acc, &acc);
        if (bit != 0U)
        {
            sb_ge_add(&acc, &acc, p);
        }
    }
    sb_ge_copy(r, &acc);
}

static void sb_ge_tobytes(uint8_t out[32], const sb_ge *p)
{
    sb_fe zinv;
    sb_fe x;
    sb_fe y;
    sb_fe_invert(&zinv, &p->z);
    sb_fe_mul(&x, &p->x, &zinv);
    sb_fe_mul(&y, &p->y, &zinv);
    sb_fe_tobytes(out, &y);
    if (sb_fe_isnegative(&x))
    {
        out[31] = (uint8_t)(out[31] | 0x80U);
    }
}

// Strict canonicity: the 255-bit LE value (top byte already masked) < p.
static bool sb_bytes_canonical_mod_p(const uint8_t y[32], uint8_t top_byte)
{
    uint32_t k;
    for (k = 0U; k < 32U; ++k)
    {
        const uint32_t i = 31U - k;
        const uint8_t yi = (i == 31U) ? top_byte : y[i];
        const uint8_t pi = (i == 31U) ? 0x7FU : ((i == 0U) ? 0xEDU : 0xFFU);
        if (yi < pi)
        {
            return true;
        }
        if (yi > pi)
        {
            return false;
        }
    }
    return false;   // equal to p: non-canonical
}

// RFC 8032 5.1.3 decompression with strict validation (mirrors C++).
static bool sb_ge_frombytes(sb_ge *r, const uint8_t s[32])
{
    const uint8_t sign = (uint8_t)(s[31] >> 7);
    const uint8_t top = (uint8_t)(s[31] & 0x7FU);
    sb_fe y;
    sb_fe y2;
    sb_fe u;
    sb_fe v;
    sb_fe x;
    sb_fe x2;
    sb_fe one;
    sb_fe vinv;
    sb_fe t;

    if (!sb_bytes_canonical_mod_p(s, top))
    {
        return false;
    }
    sb_fe_frombytes(&y, s);
    sb_fe_sq(&y2, &y);
    sb_fe_one(&one);
    sb_fe_sub(&u, &y2, &one);          // u = y^2 - 1
    sb_fe_mul(&v, &y2, &SB_FE_D);
    sb_fe_add(&v, &v, &one);           // v = d*y^2 + 1 (never 0)

    sb_fe_invert(&vinv, &v);
    sb_fe_mul(&t, &u, &vinv);          // t = u/v = x^2 candidate
    sb_fe_pow22523(&x, &t);            // t^((p-5)/8)
    sb_fe_mul(&x, &x, &t);             // t^((p+3)/8)

    sb_fe_sq(&x2, &x);
    if (!sb_fe_eq(&x2, &t))
    {
        sb_fe_mul(&x, &x, &SB_FE_SQRTM1);
        sb_fe_sq(&x2, &x);
        if (!sb_fe_eq(&x2, &t))
        {
            return false;              // off-curve
        }
    }

    if (sb_fe_iszero(&x))
    {
        if (sign != 0U)
        {
            return false;              // x == 0 with sign bit set
        }
    }
    else
    {
        if (sb_fe_isnegative(&x) != (sign != 0U))
        {
            sb_fe_neg(&x, &x);
        }
    }

    sb_fe_copy(&r->x, &x);
    sb_fe_copy(&r->y, &y);
    sb_fe_one(&r->z);
    sb_fe_mul(&r->t, &x, &y);
    return true;
}

// ---------------------------------------------------------------------------
// Scalars mod L = 2^252 + 27742317777372353535851937790883648493,
// held as 8 little-endian uint32_t words (32-bit-native).
// ---------------------------------------------------------------------------

static const uint32_t SB_SC_L[8] = {
    0x5CF5D3EDU, 0x5812631AU, 0xA2F79CD6U, 0x14DEF9DEU,
    0x00000000U, 0x00000000U, 0x00000000U, 0x10000000U
};

static bool sb_sc_geq_l(const uint32_t a[8])
{
    uint32_t k;
    for (k = 0U; k < 8U; ++k)
    {
        const uint32_t i = 7U - k;
        if (a[i] > SB_SC_L[i])
        {
            return true;
        }
        if (a[i] < SB_SC_L[i])
        {
            return false;
        }
    }
    return true;   // equal
}

static void sb_sc_sub_l(uint32_t a[8])
{
    uint32_t borrow = 0U;
    uint32_t i;
    for (i = 0U; i < 8U; ++i)
    {
        const uint32_t li = SB_SC_L[i] + borrow;   // no wrap: L_i < 2^32 - 1
        const uint32_t nb = (a[i] < li) ? 1U : 0U;
        a[i] = a[i] - li;
        borrow = nb;
    }
}

static void sb_sc_load(uint32_t a[8], const uint8_t s[32])
{
    uint32_t w;
    for (w = 0U; w < 8U; ++w)
    {
        a[w] = (uint32_t)s[4U * w]
             | ((uint32_t)s[(4U * w) + 1U] << 8)
             | ((uint32_t)s[(4U * w) + 2U] << 16)
             | ((uint32_t)s[(4U * w) + 3U] << 24);
    }
}

// Reduce a 64-byte LE value mod L by binary long division (r < L always,
// so 2r + 1 < 2L < 2^254 and one conditional subtraction suffices).
static void sb_sc_reduce_512(uint8_t out[32], const uint8_t in[64])
{
    uint32_t r[8] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
    uint32_t k;
    uint32_t w;
    for (k = 0U; k < 512U; ++k)
    {
        const uint32_t i = 511U - k;
        uint32_t carry = ((uint32_t)in[i >> 3] >> (i & 7U)) & 1U;
        for (w = 0U; w < 8U; ++w)
        {
            const uint32_t nc = r[w] >> 31;
            r[w] = (r[w] << 1) | carry;
            carry = nc;
        }
        if (sb_sc_geq_l(r))
        {
            sb_sc_sub_l(r);
        }
    }
    for (w = 0U; w < 8U; ++w)
    {
        out[4U * w]        = (uint8_t)(r[w]);
        out[(4U * w) + 1U] = (uint8_t)(r[w] >> 8);
        out[(4U * w) + 2U] = (uint8_t)(r[w] >> 16);
        out[(4U * w) + 3U] = (uint8_t)(r[w] >> 24);
    }
}

static bool sb_sc_is_canonical(const uint8_t s[32])
{
    uint32_t a[8];
    sb_sc_load(a, s);
    return !sb_sc_geq_l(a);
}

// Base point B (RFC 8032), canonical compressed encoding.
static const uint8_t SB_ED_BASEPOINT[32] = {
    0x58U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U,
    0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U,
    0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U,
    0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U
};

// ---------------------------------------------------------------------------
// Public entry point. Check order mirrors ed25519_verify() exactly.
// ---------------------------------------------------------------------------
bool sb_ed25519_verify(const uint8_t signature[64], const uint8_t *message,
                       uint64_t message_len, const uint8_t public_key[32])
{
    sb_ge a_point;
    sb_ge b_point;
    sb_ge neg_a;
    sb_ge sb_pt;
    sb_ge ka;
    sb_ge r_prime;
    Sha512State st;
    uint8_t hram[64];
    uint8_t k[32];
    uint8_t r_enc[32];
    uint8_t acc = 0U;
    uint8_t diff = 0U;
    uint32_t i;

    // All-zero public key: small-order, never a legitimate operator key.
    for (i = 0U; i < 32U; ++i)
    {
        acc = (uint8_t)(acc | public_key[i]);
    }
    if (acc == 0U)
    {
        return false;
    }
    // S must be canonical (S < L).
    if (!sb_sc_is_canonical(&signature[32]))
    {
        return false;
    }
    if (!sb_ge_frombytes(&a_point, public_key))
    {
        return false;
    }
    // Small-order public keys void attributability (REGISTRY.md §8).
    if (sb_ge_is_small_order(&a_point))
    {
        return false;
    }
    if (!sb_ge_frombytes(&b_point, SB_ED_BASEPOINT))
    {
        return false;   // cannot happen; defensive
    }

    // k = SHA-512(R || A || M) mod L
    sb_sha512_init(&st);
    sb_sha512_update(&st, signature, 32U);
    sb_sha512_update(&st, public_key, 32U);
    if (message_len > 0U)
    {
        sb_sha512_update(&st, message, message_len);
    }
    sb_sha512_final(&st, hram);
    sb_sc_reduce_512(k, hram);

    // R' = [S]B + [k](-A)
    sb_ge_neg(&neg_a, &a_point);
    sb_ge_scalarmul(&sb_pt, &signature[32], &b_point);
    sb_ge_scalarmul(&ka, k, &neg_a);
    sb_ge_add(&r_prime, &sb_pt, &ka);
    sb_ge_tobytes(r_enc, &r_prime);

    for (i = 0U; i < 32U; ++i)
    {
        diff = (uint8_t)(diff | (uint8_t)(r_enc[i] ^ signature[i]));
    }
    return diff == 0U;
}
