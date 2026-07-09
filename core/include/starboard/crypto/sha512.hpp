// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// sha512.hpp — FIPS 180-4 SHA-512, header-only, freestanding-friendly.
//
// Design constraints (flight-software profile):
//   - No exceptions, no dynamic allocation, no recursion.
//   - Fixed-size state only; the caller owns all memory.
//   - Big-endian loads/stores are done manually, byte by byte, so the code
//     is byte-order independent without relying on endian intrinsics.
//   - Incremental (init/update/final) API plus a one-shot convenience
//     wrapper, so large uplink frames can be hashed in chunks.
//
// This hash is used by the pluggable signature verifier (see
// ed25519_verify.hpp). It processes only public data (message bytes that
// are already on the bus), so a variable-time implementation is
// acceptable; the compression function below is data-independent in its
// control flow anyway.

#ifndef STARBOARD_CRYPTO_SHA512_HPP
#define STARBOARD_CRYPTO_SHA512_HPP

#include <cstdint>

namespace starboard {
namespace crypto {

struct Sha512State {
    uint64_t h[8];          // chaining value
    uint64_t total_lo;      // total message bytes processed, low 64 bits
    uint64_t total_hi;      // total message bytes processed, high 64 bits
    uint8_t buf[128];       // partial block buffer
    uint64_t buf_len;       // valid bytes in buf, 0..127
};

namespace sha512_detail {

inline constexpr uint64_t kK[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

inline uint64_t rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

inline uint64_t load_be64(const uint8_t* p) {
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; ++i) {
        r = (r << 8) | static_cast<uint64_t>(p[i]);
    }
    return r;
}

inline void store_be64(uint8_t* p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>(x >> (56u - 8u * i));
    }
}

inline void compress(uint64_t h[8], const uint8_t block[128]) {
    uint64_t w[80];
    for (unsigned i = 0; i < 16; ++i) {
        w[i] = load_be64(block + 8u * i);
    }
    for (unsigned i = 16; i < 80; ++i) {
        const uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint64_t a = h[0];
    uint64_t b = h[1];
    uint64_t c = h[2];
    uint64_t d = h[3];
    uint64_t e = h[4];
    uint64_t f = h[5];
    uint64_t g = h[6];
    uint64_t hh = h[7];

    for (unsigned i = 0; i < 80; ++i) {
        const uint64_t big_s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        const uint64_t ch = (e & f) ^ (~e & g);
        const uint64_t t1 = hh + big_s1 + ch + kK[i] + w[i];
        const uint64_t big_s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint64_t t2 = big_s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

}  // namespace sha512_detail

inline void sha512_init(Sha512State& st) {
    st.h[0] = 0x6a09e667f3bcc908ULL;
    st.h[1] = 0xbb67ae8584caa73bULL;
    st.h[2] = 0x3c6ef372fe94f82bULL;
    st.h[3] = 0xa54ff53a5f1d36f1ULL;
    st.h[4] = 0x510e527fade682d1ULL;
    st.h[5] = 0x9b05688c2b3e6c1fULL;
    st.h[6] = 0x1f83d9abfb41bd6bULL;
    st.h[7] = 0x5be0cd19137e2179ULL;
    st.total_lo = 0;
    st.total_hi = 0;
    st.buf_len = 0;
}

inline void sha512_update(Sha512State& st, const uint8_t* data, uint64_t len) {
    st.total_lo += len;
    if (st.total_lo < len) {
        st.total_hi += 1;
    }

    uint64_t off = 0;
    if (st.buf_len > 0) {
        uint64_t need = 128u - st.buf_len;
        if (need > len) {
            need = len;
        }
        for (uint64_t i = 0; i < need; ++i) {
            st.buf[st.buf_len + i] = data[i];
        }
        st.buf_len += need;
        off += need;
        if (st.buf_len == 128u) {
            sha512_detail::compress(st.h, st.buf);
            st.buf_len = 0;
        }
    }
    while (len - off >= 128u) {
        sha512_detail::compress(st.h, data + off);
        off += 128u;
    }
    while (off < len) {
        st.buf[st.buf_len] = data[off];
        st.buf_len += 1;
        off += 1;
    }
}

inline void sha512_final(Sha512State& st, uint8_t out[64]) {
    // Message length in bits, as a 128-bit big-endian quantity.
    const uint64_t bits_hi = (st.total_hi << 3) | (st.total_lo >> 61);
    const uint64_t bits_lo = st.total_lo << 3;

    // Append 0x80, then zeros, so that 16 bytes remain for the length.
    uint8_t pad[128];
    pad[0] = 0x80u;
    uint64_t pad_len = 1;
    const uint64_t used = st.buf_len;
    uint64_t zero_bytes = 0;
    if (used < 112u) {
        zero_bytes = 112u - used - 1u;
    } else {
        zero_bytes = 240u - used - 1u;
    }
    for (uint64_t i = 0; i < zero_bytes; ++i) {
        pad[pad_len] = 0;
        pad_len += 1;
    }
    sha512_update(st, pad, pad_len);

    uint8_t len_block[16];
    sha512_detail::store_be64(len_block, bits_hi);
    sha512_detail::store_be64(len_block + 8, bits_lo);
    sha512_update(st, len_block, 16);

    for (unsigned i = 0; i < 8; ++i) {
        sha512_detail::store_be64(out + 8u * i, st.h[i]);
    }
}

inline void sha512(const uint8_t* msg, uint64_t len, uint8_t out[64]) {
    Sha512State st;
    sha512_init(st);
    sha512_update(st, msg, len);
    sha512_final(st, out);
}

}  // namespace crypto
}  // namespace starboard

#endif  // STARBOARD_CRYPTO_SHA512_HPP
