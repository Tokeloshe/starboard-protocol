// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// ed25519_verify.hpp — RFC 8032 Ed25519 signature VERIFICATION only.
//
// Scope and design constraints (flight-software profile):
//   - Verify only. No signing, no key generation, no secret material ever
//     enters this module. This is the reference backend for the pluggable
//     uplink-command verifier; alternative backends (hardware crypto, a
//     certified library) may be substituted behind the same entry point.
//   - Because every input (signature, message, public key) is public bus
//     data, variable-time arithmetic is acceptable here: there is no
//     secret for a timing side channel to leak. Scalar multiplication is
//     therefore a simple, auditable double-and-add. The fixed-exponent
//     field ladders (inversion via pow(p-2), square root via
//     pow((p+3)/8)) have data-independent control flow by construction.
//   - No exceptions, no dynamic allocation, no recursion, fixed-size
//     arrays only.
//   - Field arithmetic mod p = 2^255 - 19 uses 5 unsigned 51-bit limbs
//     with unsigned __int128 products (GCC/Clang extension).
//   - Points use extended twisted-Edwards coordinates (X:Y:Z:T) with the
//     complete/unified addition formulas for a = -1 (add-2008-hwcd-3),
//     which are also used for doubling.
//   - Cofactorless verification: R' = [S]B - [k]A is computed and its
//     canonical encoding is compared with the transmitted R bytes.
//   - Strict input validation: S >= L is rejected (signature
//     malleability), non-canonical y (>= p) is rejected, off-curve points
//     are rejected, and x-sign-bit set with x == 0 is rejected. The
//     all-zero public key (a small-order point) is rejected explicitly.

#ifndef STARBOARD_CRYPTO_ED25519_VERIFY_HPP
#define STARBOARD_CRYPTO_ED25519_VERIFY_HPP

#include <cstdint>

#include "sha512.hpp"

namespace starboard {
namespace crypto {
namespace ed25519_detail {

// ---------------------------------------------------------------------------
// Field arithmetic mod p = 2^255 - 19, radix 2^51, 5 unsigned limbs.
// Invariant maintained by fe_carry: every limb < 2^52 (loosely reduced).
// ---------------------------------------------------------------------------

struct Fe {
    uint64_t v[5];
};

inline constexpr uint64_t kMask51 = 0x0007FFFFFFFFFFFFULL;

// d = -121665/121666 mod p
inline constexpr Fe kFeD = {{
    0x34DCA135978A3ULL, 0x1A8283B156EBDULL, 0x5E7A26001C029ULL,
    0x739C663A03CBBULL, 0x52036CEE2B6FFULL
}};

// 2*d mod p
inline constexpr Fe kFeD2 = {{
    0x69B9426B2F159ULL, 0x35050762ADD7AULL, 0x3CF44C0038052ULL,
    0x6738CC7407977ULL, 0x2406D9DC56DFFULL
}};

// sqrt(-1) = 2^((p-1)/4) mod p
inline constexpr Fe kFeSqrtM1 = {{
    0x61B274A0EA0B0ULL, 0x0D5A5FC8F189DULL, 0x7EF5E9CBD0C60ULL,
    0x78595A6804C9EULL, 0x2B8324804FC1DULL
}};

inline void fe_zero(Fe& r) {
    for (unsigned i = 0; i < 5; ++i) {
        r.v[i] = 0;
    }
}

inline void fe_one(Fe& r) {
    r.v[0] = 1;
    for (unsigned i = 1; i < 5; ++i) {
        r.v[i] = 0;
    }
}

inline void fe_copy(Fe& r, const Fe& a) {
    for (unsigned i = 0; i < 5; ++i) {
        r.v[i] = a.v[i];
    }
}

// Carry chain: after this, limbs 0..4 are < 2^51 except that limb 1 may be
// up to 2^51 (inclusive); all limbs are < 2^52, which every routine below
// accepts as input.
inline void fe_carry(Fe& r) {
    uint64_t c;
    c = r.v[0] >> 51; r.v[0] &= kMask51; r.v[1] += c;
    c = r.v[1] >> 51; r.v[1] &= kMask51; r.v[2] += c;
    c = r.v[2] >> 51; r.v[2] &= kMask51; r.v[3] += c;
    c = r.v[3] >> 51; r.v[3] &= kMask51; r.v[4] += c;
    c = r.v[4] >> 51; r.v[4] &= kMask51; r.v[0] += c * 19u;
    c = r.v[0] >> 51; r.v[0] &= kMask51; r.v[1] += c;
}

inline void fe_add(Fe& r, const Fe& a, const Fe& b) {
    for (unsigned i = 0; i < 5; ++i) {
        r.v[i] = a.v[i] + b.v[i];
    }
    fe_carry(r);
}

// r = a - b. Adds 4*p limb-wise so the subtraction cannot underflow for
// any loosely reduced inputs (limbs < 2^52).
inline void fe_sub(Fe& r, const Fe& a, const Fe& b) {
    // 4*p = (2^53 - 76) + (2^53 - 4)*2^51 + ... limb-wise doubling of 2*p.
    r.v[0] = a.v[0] + 0x1FFFFFFFFFFFB4ULL - b.v[0];
    r.v[1] = a.v[1] + 0x1FFFFFFFFFFFFCULL - b.v[1];
    r.v[2] = a.v[2] + 0x1FFFFFFFFFFFFCULL - b.v[2];
    r.v[3] = a.v[3] + 0x1FFFFFFFFFFFFCULL - b.v[3];
    r.v[4] = a.v[4] + 0x1FFFFFFFFFFFFCULL - b.v[4];
    fe_carry(r);
}

inline void fe_neg(Fe& r, const Fe& a) {
    Fe zero;
    fe_zero(zero);
    fe_sub(r, zero, a);
}

inline void fe_mul(Fe& r, const Fe& a, const Fe& b) {
    __extension__ typedef unsigned __int128 u128;
    const uint64_t a0 = a.v[0], a1 = a.v[1], a2 = a.v[2], a3 = a.v[3], a4 = a.v[4];
    const uint64_t b0 = b.v[0], b1 = b.v[1], b2 = b.v[2], b3 = b.v[3], b4 = b.v[4];
    const uint64_t b1_19 = b1 * 19u;
    const uint64_t b2_19 = b2 * 19u;
    const uint64_t b3_19 = b3 * 19u;
    const uint64_t b4_19 = b4 * 19u;

    u128 t0 = (u128)a0 * b0 + (u128)a1 * b4_19 + (u128)a2 * b3_19 + (u128)a3 * b2_19 + (u128)a4 * b1_19;
    u128 t1 = (u128)a0 * b1 + (u128)a1 * b0 + (u128)a2 * b4_19 + (u128)a3 * b3_19 + (u128)a4 * b2_19;
    u128 t2 = (u128)a0 * b2 + (u128)a1 * b1 + (u128)a2 * b0 + (u128)a3 * b4_19 + (u128)a4 * b3_19;
    u128 t3 = (u128)a0 * b3 + (u128)a1 * b2 + (u128)a2 * b1 + (u128)a3 * b0 + (u128)a4 * b4_19;
    u128 t4 = (u128)a0 * b4 + (u128)a1 * b3 + (u128)a2 * b2 + (u128)a3 * b1 + (u128)a4 * b0;

    uint64_t r0, r1, r2, r3, r4, c;
    r0 = (uint64_t)t0 & kMask51; t1 += (uint64_t)(t0 >> 51);
    r1 = (uint64_t)t1 & kMask51; t2 += (uint64_t)(t1 >> 51);
    r2 = (uint64_t)t2 & kMask51; t3 += (uint64_t)(t2 >> 51);
    r3 = (uint64_t)t3 & kMask51; t4 += (uint64_t)(t3 >> 51);
    r4 = (uint64_t)t4 & kMask51;
    c = (uint64_t)(t4 >> 51);
    r0 += c * 19u;
    c = r0 >> 51; r0 &= kMask51; r1 += c;

    r.v[0] = r0;
    r.v[1] = r1;
    r.v[2] = r2;
    r.v[3] = r3;
    r.v[4] = r4;
}

inline void fe_sq(Fe& r, const Fe& a) {
    fe_mul(r, a, a);
}

// Fully reduce and serialize to 32 little-endian bytes (canonical, < p).
inline void fe_tobytes(uint8_t out[32], const Fe& a) {
    Fe t;
    fe_copy(t, a);
    fe_carry(t);
    fe_carry(t);
    // Now every limb is < 2^51. Compute q = 1 iff t >= p, then add 19*q
    // and drop bit 255 to get the canonical residue.
    uint64_t q = (t.v[0] + 19u) >> 51;
    q = (t.v[1] + q) >> 51;
    q = (t.v[2] + q) >> 51;
    q = (t.v[3] + q) >> 51;
    q = (t.v[4] + q) >> 51;
    t.v[0] += 19u * q;
    uint64_t c;
    c = t.v[0] >> 51; t.v[0] &= kMask51; t.v[1] += c;
    c = t.v[1] >> 51; t.v[1] &= kMask51; t.v[2] += c;
    c = t.v[2] >> 51; t.v[2] &= kMask51; t.v[3] += c;
    c = t.v[3] >> 51; t.v[3] &= kMask51; t.v[4] += c;
    t.v[4] &= kMask51;

    const uint64_t w0 = t.v[0] | (t.v[1] << 51);
    const uint64_t w1 = (t.v[1] >> 13) | (t.v[2] << 38);
    const uint64_t w2 = (t.v[2] >> 26) | (t.v[3] << 25);
    const uint64_t w3 = (t.v[3] >> 39) | (t.v[4] << 12);
    const uint64_t w[4] = { w0, w1, w2, w3 };
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 8; ++j) {
            out[8u * i + j] = (uint8_t)(w[i] >> (8u * j));
        }
    }
}

// Deserialize 32 little-endian bytes; bit 255 is ignored (it carries the
// x sign in point encodings). Does NOT check canonicity — callers that
// need strict canonicity check the raw bytes against p first.
inline void fe_frombytes(Fe& r, const uint8_t in[32]) {
    uint64_t w[4];
    for (unsigned i = 0; i < 4; ++i) {
        uint64_t x = 0;
        for (unsigned j = 0; j < 8; ++j) {
            x |= (uint64_t)in[8u * i + j] << (8u * j);
        }
        w[i] = x;
    }
    r.v[0] = w[0] & kMask51;
    r.v[1] = ((w[0] >> 51) | (w[1] << 13)) & kMask51;
    r.v[2] = ((w[1] >> 38) | (w[2] << 26)) & kMask51;
    r.v[3] = ((w[2] >> 25) | (w[3] << 39)) & kMask51;
    r.v[4] = (w[3] >> 12) & kMask51;
}

inline bool fe_iszero(const Fe& a) {
    uint8_t b[32];
    fe_tobytes(b, a);
    uint8_t acc = 0;
    for (unsigned i = 0; i < 32; ++i) {
        acc |= b[i];
    }
    return acc == 0;
}

inline bool fe_eq(const Fe& a, const Fe& b) {
    uint8_t ba[32];
    uint8_t bb[32];
    fe_tobytes(ba, a);
    fe_tobytes(bb, b);
    uint8_t acc = 0;
    for (unsigned i = 0; i < 32; ++i) {
        acc |= (uint8_t)(ba[i] ^ bb[i]);
    }
    return acc == 0;
}

// Least significant bit of the canonical representation ("negative" per
// RFC 8032 sign convention).
inline bool fe_isnegative(const Fe& a) {
    uint8_t b[32];
    fe_tobytes(b, a);
    return (b[0] & 1u) != 0;
}

inline void fe_sq_n(Fe& r, const Fe& a, unsigned n) {
    fe_copy(r, a);
    for (unsigned i = 0; i < n; ++i) {
        fe_sq(r, r);
    }
}

// r = z^(2^252 - 3) = z^((p-5)/8). Fixed addition chain (data-independent
// control flow; the exponent is a public constant).
inline void fe_pow22523(Fe& r, const Fe& z) {
    Fe t0, t1, t2;
    fe_sq(t0, z);                 // z^2
    fe_sq_n(t1, t0, 2);           // z^8
    fe_mul(t1, z, t1);            // z^9
    fe_mul(t0, t0, t1);           // z^11
    fe_sq(t0, t0);                // z^22
    fe_mul(t0, t1, t0);           // z^(2^5 - 1)
    fe_sq_n(t1, t0, 5);
    fe_mul(t0, t1, t0);           // z^(2^10 - 1)
    fe_sq_n(t1, t0, 10);
    fe_mul(t1, t1, t0);           // z^(2^20 - 1)
    fe_sq_n(t2, t1, 20);
    fe_mul(t1, t2, t1);           // z^(2^40 - 1)
    fe_sq_n(t1, t1, 10);
    fe_mul(t0, t1, t0);           // z^(2^50 - 1)
    fe_sq_n(t1, t0, 50);
    fe_mul(t1, t1, t0);           // z^(2^100 - 1)
    fe_sq_n(t2, t1, 100);
    fe_mul(t1, t2, t1);           // z^(2^200 - 1)
    fe_sq_n(t1, t1, 50);
    fe_mul(t0, t1, t0);           // z^(2^250 - 1)
    fe_sq_n(t0, t0, 2);           // z^(2^252 - 4)
    fe_mul(r, t0, z);             // z^(2^252 - 3)
}

// r = z^(p-2) = z^(2^255 - 21), i.e. the modular inverse for z != 0.
// Fixed addition chain (data-independent control flow).
inline void fe_invert(Fe& r, const Fe& z) {
    Fe t0, t1, t2, t3;
    fe_sq(t0, z);                 // z^2
    fe_sq_n(t1, t0, 2);           // z^8
    fe_mul(t1, z, t1);            // z^9
    fe_mul(t0, t0, t1);           // z^11
    fe_sq(t2, t0);                // z^22
    fe_mul(t1, t1, t2);           // z^(2^5 - 1)
    fe_sq_n(t2, t1, 5);
    fe_mul(t1, t2, t1);           // z^(2^10 - 1)
    fe_sq_n(t2, t1, 10);
    fe_mul(t2, t2, t1);           // z^(2^20 - 1)
    fe_sq_n(t3, t2, 20);
    fe_mul(t2, t3, t2);           // z^(2^40 - 1)
    fe_sq_n(t2, t2, 10);
    fe_mul(t1, t2, t1);           // z^(2^50 - 1)
    fe_sq_n(t2, t1, 50);
    fe_mul(t2, t2, t1);           // z^(2^100 - 1)
    fe_sq_n(t3, t2, 100);
    fe_mul(t2, t3, t2);           // z^(2^200 - 1)
    fe_sq_n(t2, t2, 50);
    fe_mul(t1, t2, t1);           // z^(2^250 - 1)
    fe_sq_n(t1, t1, 5);           // z^(2^255 - 32)
    fe_mul(r, t1, t0);            // z^(2^255 - 21)
}

// ---------------------------------------------------------------------------
// Group: extended twisted-Edwards coordinates (X:Y:Z:T), T = X*Y/Z,
// for -x^2 + y^2 = 1 + d*x^2*y^2.
// ---------------------------------------------------------------------------

struct GePoint {
    Fe x;
    Fe y;
    Fe z;
    Fe t;
};

inline void ge_identity(GePoint& r) {
    fe_zero(r.x);
    fe_one(r.y);
    fe_one(r.z);
    fe_zero(r.t);
}

inline void ge_copy(GePoint& r, const GePoint& a) {
    fe_copy(r.x, a.x);
    fe_copy(r.y, a.y);
    fe_copy(r.z, a.z);
    fe_copy(r.t, a.t);
}

inline void ge_neg(GePoint& r, const GePoint& a) {
    fe_neg(r.x, a.x);
    fe_copy(r.y, a.y);
    fe_copy(r.z, a.z);
    fe_neg(r.t, a.t);
}

// Unified/complete addition, add-2008-hwcd-3 (a = -1). Because a = -1 is a
// square and d is a non-square mod p, this formula is complete for all
// curve points, so it is also used for doubling. Safe for r aliasing p or q.
inline void ge_add(GePoint& r, const GePoint& p, const GePoint& q) {
    Fe a, b, c, dd, e, f, g, h, t0, t1;
    fe_sub(t0, p.y, p.x);
    fe_sub(t1, q.y, q.x);
    fe_mul(a, t0, t1);            // A = (Y1-X1)*(Y2-X2)
    fe_add(t0, p.y, p.x);
    fe_add(t1, q.y, q.x);
    fe_mul(b, t0, t1);            // B = (Y1+X1)*(Y2+X2)
    fe_mul(c, p.t, q.t);
    fe_mul(c, c, kFeD2);          // C = 2d*T1*T2
    fe_mul(dd, p.z, q.z);
    fe_add(dd, dd, dd);           // D = 2*Z1*Z2
    fe_sub(e, b, a);              // E = B - A
    fe_sub(f, dd, c);             // F = D - C
    fe_add(g, dd, c);             // G = D + C
    fe_add(h, b, a);              // H = B + A
    fe_mul(r.x, e, f);
    fe_mul(r.y, g, h);
    fe_mul(r.t, e, h);
    fe_mul(r.z, f, g);
}

// Variable-time scalar multiplication (MSB-first double-and-add).
// scalar is 32 little-endian bytes. Acceptable because all inputs to
// verification are public data.
inline void ge_scalarmul(GePoint& r, const uint8_t scalar[32], const GePoint& p) {
    GePoint acc;
    ge_identity(acc);
    for (int i = 255; i >= 0; --i) {
        ge_add(acc, acc, acc);
        const unsigned bit = (scalar[i >> 3] >> (i & 7)) & 1u;
        if (bit != 0u) {
            ge_add(acc, acc, p);
        }
    }
    ge_copy(r, acc);
}

// Canonical point encoding: 32 bytes, y little-endian with the sign of x
// in bit 255.
inline void ge_tobytes(uint8_t out[32], const GePoint& p) {
    Fe zinv, x, y;
    fe_invert(zinv, p.z);
    fe_mul(x, p.x, zinv);
    fe_mul(y, p.y, zinv);
    fe_tobytes(out, y);
    if (fe_isnegative(x)) {
        out[31] |= 0x80u;
    }
}

// Strict canonicity check for a 255-bit little-endian value (sign bit
// already stripped by the caller): value must be < p = 2^255 - 19.
inline bool bytes_canonical_mod_p(const uint8_t y[32], uint8_t top_byte) {
    uint8_t pb[32];
    pb[0] = 0xEDu;
    for (unsigned i = 1; i < 31; ++i) {
        pb[i] = 0xFFu;
    }
    pb[31] = 0x7Fu;
    // Compare (y with masked top byte) against p, most significant first.
    for (int i = 31; i >= 0; --i) {
        const uint8_t yi = (i == 31) ? top_byte : y[i];
        if (yi < pb[i]) {
            return true;
        }
        if (yi > pb[i]) {
            return false;
        }
    }
    return false;  // equal to p: non-canonical
}

// Decompress a point per RFC 8032 5.1.3 with strict validation.
// Returns false for: non-canonical y (>= p), x^2 with no square root
// (off-curve), and sign bit set with x == 0.
inline bool ge_frombytes(GePoint& r, const uint8_t s[32]) {
    const uint8_t sign = (uint8_t)(s[31] >> 7);
    const uint8_t top = (uint8_t)(s[31] & 0x7Fu);
    if (!bytes_canonical_mod_p(s, top)) {
        return false;
    }

    Fe y, y2, u, v, x, x2;
    fe_frombytes(y, s);           // ignores bit 255
    fe_sq(y2, y);
    Fe one;
    fe_one(one);
    fe_sub(u, y2, one);           // u = y^2 - 1
    fe_mul(v, y2, kFeD);
    fe_add(v, v, one);            // v = d*y^2 + 1  (never 0: -1/d is non-square)

    // x = (u/v)^((p+3)/8) = (u/v) * (u/v)^((p-5)/8), computed via a full
    // inversion of v followed by the fixed pow22523 ladder.
    Fe vinv, t;
    fe_invert(vinv, v);
    fe_mul(t, u, vinv);           // t = u/v = x^2 candidate
    fe_pow22523(x, t);            // t^((p-5)/8)
    fe_mul(x, x, t);              // t^((p+3)/8)

    fe_sq(x2, x);
    if (!fe_eq(x2, t)) {
        fe_mul(x, x, kFeSqrtM1);  // sqrt(-1) correction
        fe_sq(x2, x);
        if (!fe_eq(x2, t)) {
            return false;         // not a square: point is off-curve
        }
    }

    if (fe_iszero(x)) {
        if (sign != 0u) {
            return false;         // x == 0 with sign bit set is invalid
        }
    } else {
        if (fe_isnegative(x) != (sign != 0u)) {
            fe_neg(x, x);
        }
    }

    fe_copy(r.x, x);
    fe_copy(r.y, y);
    fe_one(r.z);
    fe_mul(r.t, x, y);
    return true;
}

// ---------------------------------------------------------------------------
// Scalar arithmetic mod L = 2^252 + 27742317777372353535851937790883648493.
// ---------------------------------------------------------------------------

inline constexpr uint64_t kL[4] = {
    0x5812631A5CF5D3EDULL, 0x14DEF9DEA2F79CD6ULL,
    0x0000000000000000ULL, 0x1000000000000000ULL
};

// true iff the 256-bit little-endian value a >= L.
inline bool sc256_geq_l(const uint64_t a[4]) {
    for (int i = 3; i >= 0; --i) {
        if (a[i] > kL[i]) {
            return true;
        }
        if (a[i] < kL[i]) {
            return false;
        }
    }
    return true;  // equal
}

inline void sc256_sub_l(uint64_t a[4]) {
    uint64_t borrow = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint64_t li = kL[i] + borrow;
        // li cannot wrap: kL[i] < 2^64 - 1 for all i.
        borrow = (a[i] < li) ? 1u : 0u;
        a[i] = a[i] - li;
    }
}

// Reduce a 64-byte little-endian value mod L by binary long division:
// obviously correct, and this is not a hot path. Output: 32 LE bytes.
inline void sc_reduce_512(uint8_t out[32], const uint8_t in[64]) {
    uint64_t r[4] = { 0, 0, 0, 0 };
    for (int i = 511; i >= 0; --i) {
        // r = r*2 + bit_i; r stays < 2*L < 2^254, so no word overflow.
        uint64_t carry = 0;
        for (unsigned w = 0; w < 4; ++w) {
            const uint64_t nc = r[w] >> 63;
            r[w] = (r[w] << 1) | carry;
            carry = nc;
        }
        r[0] |= (uint64_t)((in[i >> 3] >> (i & 7)) & 1u);
        if (sc256_geq_l(r)) {
            sc256_sub_l(r);
        }
    }
    for (unsigned w = 0; w < 4; ++w) {
        for (unsigned j = 0; j < 8; ++j) {
            out[8u * w + j] = (uint8_t)(r[w] >> (8u * j));
        }
    }
}

// true iff the 32 little-endian bytes encode a value < L.
inline bool sc_is_canonical(const uint8_t s[32]) {
    uint64_t a[4];
    for (unsigned w = 0; w < 4; ++w) {
        uint64_t x = 0;
        for (unsigned j = 0; j < 8; ++j) {
            x |= (uint64_t)s[8u * w + j] << (8u * j);
        }
        a[w] = x;
    }
    return !sc256_geq_l(a);
}

// Base point B (RFC 8032): y = 4/5, x positive-even root; canonical
// compressed encoding.
inline constexpr uint8_t kBasePoint[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};

}  // namespace ed25519_detail

// RFC 8032 Ed25519 verification (cofactorless): accepts iff the signature
// is strictly canonical (S < L, R canonical by encoding comparison), the
// public key decodes to a curve point, and [S]B - [k]A encodes to R.
inline bool ed25519_verify(const uint8_t signature[64], const uint8_t* message,
                           uint64_t message_len, const uint8_t public_key[32]) {
    using namespace ed25519_detail;

    // Reject the all-zero public key outright: it encodes a small-order
    // point and is never a legitimate operator key.
    {
        uint8_t acc = 0;
        for (unsigned i = 0; i < 32; ++i) {
            acc |= public_key[i];
        }
        if (acc == 0) {
            return false;
        }
    }

    // S must be a canonical scalar (S < L); rejects malleated signatures.
    if (!sc_is_canonical(signature + 32)) {
        return false;
    }

    GePoint a_point;
    if (!ge_frombytes(a_point, public_key)) {
        return false;
    }

    GePoint b_point;
    if (!ge_frombytes(b_point, kBasePoint)) {
        return false;  // cannot happen; defensive
    }

    // k = SHA-512(R || A || M) mod L
    uint8_t hram[64];
    Sha512State st;
    sha512_init(st);
    sha512_update(st, signature, 32);
    sha512_update(st, public_key, 32);
    if (message_len > 0) {
        sha512_update(st, message, message_len);
    }
    sha512_final(st, hram);

    uint8_t k[32];
    sc_reduce_512(k, hram);

    // R' = [S]B + [k](-A)
    GePoint neg_a;
    ge_neg(neg_a, a_point);

    GePoint sb, ka, r_prime;
    ge_scalarmul(sb, signature + 32, b_point);
    ge_scalarmul(ka, k, neg_a);
    ge_add(r_prime, sb, ka);

    uint8_t r_enc[32];
    ge_tobytes(r_enc, r_prime);

    uint8_t diff = 0;
    for (unsigned i = 0; i < 32; ++i) {
        diff |= (uint8_t)(r_enc[i] ^ signature[i]);
    }
    return diff == 0;
}

}  // namespace crypto
}  // namespace starboard

#endif  // STARBOARD_CRYPTO_ED25519_VERIFY_HPP
