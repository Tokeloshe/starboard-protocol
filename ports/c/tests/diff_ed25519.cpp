// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// diff_ed25519.cpp — DIFFERENTIAL test: the C99 port's sb_ed25519_verify
// (ports/c/ed25519.c, 10-limb radix-2^25.5, linked via extern "C") against
// the C++ reference starboard::crypto::ed25519_verify (5-limb radix-2^51,
// unsigned __int128). For every case both must return the IDENTICAL bool.
//
// Corpora (fixed-seed splitmix64, fully reproducible):
//   A  seed mutation: RFC 8032 TEST 1/2/3 + registry fixture signatures,
//      1..4 random bit flips across sig / key / message (some cases
//      unmutated so the accept path is exercised continuously);
//   B  fully random 32-byte keys and 64-byte signatures, random messages;
//   C  random keys + random messages + VALID signatures from a test-only
//      signer built from the C++ header's own primitives (both must accept),
//      then the same signatures with one bit flipped (both must reject);
//   D  structured edge cases: torsion-point keys and their non-canonical /
//      sign-bit variants, y >= p keys, S in {L-1, L, L+1, 2^253-1, ...},
//      mixed-order keys (valid key + torsion point).
// Host-side test tooling; not flight code.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "starboard/crypto/ed25519_verify.hpp"

extern "C" bool sb_ed25519_verify(const uint8_t signature[64],
                                  const uint8_t *message, uint64_t message_len,
                                  const uint8_t public_key[32]);

namespace {

namespace ed = starboard::crypto::ed25519_detail;
namespace cr = starboard::crypto;

uint64_t g_rng = 0x5EB0A2D5EB0A2D01ull;
uint64_t rnd() {
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
uint32_t rnd_below(uint32_t n) { return static_cast<uint32_t>(rnd() % n); }
void rnd_bytes(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) { p[i] = static_cast<uint8_t>(rnd()); }
}

uint32_t hex_decode(const char* hex, uint8_t* out, uint32_t cap) {
    uint32_t n = 0;
    const auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
        if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(c - 'a' + 10); }
        return static_cast<uint8_t>(c - 'A' + 10);
    };
    while (hex[2 * n] != '\0' && n < cap) {
        out[n] = static_cast<uint8_t>((nib(hex[2 * n]) << 4) | nib(hex[2 * n + 1]));
        ++n;
    }
    return n;
}

struct Stats {
    uint64_t cases = 0, accept = 0, reject = 0, mismatch = 0;
};

// Run both verifiers; record; return the (common) result.
bool both(Stats& st, const char* corpus, const uint8_t sig[64],
          const uint8_t* msg, uint64_t mlen, const uint8_t pub[32]) {
    const bool c = sb_ed25519_verify(sig, msg, mlen, pub);
    const bool cpp = cr::ed25519_verify(sig, msg, mlen, pub);
    ++st.cases;
    if (c != cpp) {
        ++st.mismatch;
        if (st.mismatch <= 10) {
            std::printf("MISMATCH [%s] C=%d C++=%d pub=", corpus, c, cpp);
            for (int i = 0; i < 32; ++i) { std::printf("%02x", pub[i]); }
            std::printf(" sig=");
            for (int i = 0; i < 64; ++i) { std::printf("%02x", sig[i]); }
            std::printf(" mlen=%llu\n", static_cast<unsigned long long>(mlen));
        }
    }
    if (c) { ++st.accept; } else { ++st.reject; }
    return c;
}

void report(const char* name, const Stats& s) {
    std::printf("  %-34s cases=%-6llu accept=%-6llu reject=%-6llu mismatch=%llu\n",
                name, static_cast<unsigned long long>(s.cases),
                static_cast<unsigned long long>(s.accept),
                static_cast<unsigned long long>(s.reject),
                static_cast<unsigned long long>(s.mismatch));
}

// --- Test-only signer (RFC 8032 §5.1.6) from the C++ header primitives. ----
// k*a + r mod L with 32-bit schoolbook (no secret-handling concerns: test).
void sc_muladd(uint8_t out[32], const uint8_t a[32], const uint8_t b[32],
               const uint8_t c[32]) {
    uint32_t aw[8], bw[8];
    for (int i = 0; i < 8; ++i) {
        aw[i] = bw[i] = 0;
        for (int j = 0; j < 4; ++j) {
            aw[i] |= static_cast<uint32_t>(a[4 * i + j]) << (8 * j);
            bw[i] |= static_cast<uint32_t>(b[4 * i + j]) << (8 * j);
        }
    }
    uint64_t prod[17] = {0};
    for (int i = 0; i < 8; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < 8; ++j) {
            const uint64_t t = static_cast<uint64_t>(aw[i]) * bw[j] +
                               (prod[i + j] & 0xFFFFFFFFull) + carry;
            prod[i + j] = t & 0xFFFFFFFFull;
            carry = t >> 32;
        }
        prod[i + 8] += carry;
    }
    // + c (c < 2^256; product < 2^511, so no overflow past 512 bits)
    uint64_t carry = 0;
    for (int i = 0; i < 16; ++i) {
        uint64_t ci = 0;
        if (i < 8) {
            for (int j = 0; j < 4; ++j) {
                ci |= static_cast<uint64_t>(c[4 * i + j]) << (8 * j);
            }
        }
        const uint64_t t = prod[i] + ci + carry;
        prod[i] = t & 0xFFFFFFFFull;
        carry = t >> 32;
    }
    uint8_t wide[64];
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 4; ++j) {
            wide[4 * i + j] = static_cast<uint8_t>(prod[i] >> (8 * j));
        }
    }
    ed::sc_reduce_512(out, wide);
}

void ed_sign(uint8_t sig[64], uint8_t pub[32], const uint8_t seed[32],
             const uint8_t* msg, uint64_t mlen) {
    uint8_t h[64];
    cr::sha512(seed, 32, h);
    h[0] &= 248u;
    h[31] &= 127u;
    h[31] |= 64u;
    ed::GePoint B, A, R;
    (void)ed::ge_frombytes(B, ed::kBasePoint);
    ed::ge_scalarmul(A, h, B);
    ed::ge_tobytes(pub, A);
    uint8_t rh[64], r[32];
    cr::Sha512State st;
    cr::sha512_init(st);
    cr::sha512_update(st, h + 32, 32);
    if (mlen > 0) { cr::sha512_update(st, msg, mlen); }
    cr::sha512_final(st, rh);
    ed::sc_reduce_512(r, rh);
    ed::ge_scalarmul(R, r, B);
    ed::ge_tobytes(sig, R);
    uint8_t kh[64], k[32];
    cr::sha512_init(st);
    cr::sha512_update(st, sig, 32);
    cr::sha512_update(st, pub, 32);
    if (mlen > 0) { cr::sha512_update(st, msg, mlen); }
    cr::sha512_final(st, kh);
    ed::sc_reduce_512(k, kh);
    sc_muladd(sig + 32, k, h, r);   // S = k*a + r mod L
}

struct Seed {
    uint8_t pub[32];
    uint8_t sig[64];
    uint8_t msg[200];
    uint32_t mlen;
};

}  // namespace

int main() {
    bool ok = true;

    // --- Signer self-check against RFC 8032 TEST 1..3 (secret seeds). ----
    struct Rfc { const char* seed; const char* pub; const char* msg; const char* sig; };
    const Rfc rfc[3] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
         "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
         "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
         "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    Seed seeds[8];
    uint32_t nseeds = 0;
    for (const Rfc& v : rfc) {
        uint8_t sk[32], pub[32], sig[64], msg[8], mypub[32], mysig[64];
        (void)hex_decode(v.seed, sk, 32);
        (void)hex_decode(v.pub, pub, 32);
        (void)hex_decode(v.sig, sig, 64);
        const uint32_t mlen = hex_decode(v.msg, msg, 8);
        ed_sign(mysig, mypub, sk, msg, mlen);
        if (std::memcmp(pub, mypub, 32) != 0 || std::memcmp(sig, mysig, 64) != 0) {
            std::printf("FAIL: test signer does not reproduce RFC 8032 vector\n");
            ok = false;
        }
        Seed& s = seeds[nseeds++];
        std::memcpy(s.pub, pub, 32);
        std::memcpy(s.sig, sig, 64);
        std::memcpy(s.msg, msg, mlen);
        s.mlen = mlen;
    }
    std::printf("test signer reproduces RFC 8032 TEST 1-3: %s\n", ok ? "yes" : "NO");

    // Extra seeds: signer-made signatures over longer messages (cover
    // multi-block SHA-512 paths in the mutation corpus).
    for (uint32_t i = 0; i < 3; ++i) {
        Seed& s = seeds[nseeds++];
        uint8_t sk[32];
        rnd_bytes(sk, 32);
        s.mlen = 64 + 60 * i;
        rnd_bytes(s.msg, s.mlen);
        ed_sign(s.sig, s.pub, sk, s.msg, s.mlen);
    }

    // ---------------- A: seed mutation ----------------
    Stats sa;
    for (uint32_t it = 0; it < 12000; ++it) {
        const Seed& s = seeds[rnd_below(nseeds)];
        uint8_t pub[32], sig[64], msg[200];
        std::memcpy(pub, s.pub, 32);
        std::memcpy(sig, s.sig, 64);
        std::memcpy(msg, s.msg, s.mlen);
        uint32_t mlen = s.mlen;
        const uint32_t flips = (it % 16 == 0) ? 0 : 1 + rnd_below(4);
        for (uint32_t f = 0; f < flips; ++f) {
            const uint32_t where = rnd_below(3);
            const uint8_t bit = static_cast<uint8_t>(1u << rnd_below(8));
            if (where == 0) { sig[rnd_below(64)] ^= bit; }
            else if (where == 1) { pub[rnd_below(32)] ^= bit; }
            else if (mlen > 0) { msg[rnd_below(mlen)] ^= bit; }
            else { mlen = 1; msg[0] = bit; }   // append a byte to empty msg
        }
        (void)both(sa, "A", sig, msg, mlen, pub);
    }
    report("A seed mutation", sa);

    // ---------------- B: fully random ----------------
    Stats sb;
    for (uint32_t it = 0; it < 6000; ++it) {
        uint8_t pub[32], sig[64], msg[64];
        rnd_bytes(pub, 32);
        rnd_bytes(sig, 64);
        if (it % 2 == 0) { sig[63] &= 0x0Fu; }   // half with S < 2^252 (often < L)
        const uint32_t mlen = rnd_below(64);
        rnd_bytes(msg, mlen);
        (void)both(sb, "B", sig, msg, mlen, pub);
    }
    report("B fully random key/sig", sb);

    // ---------------- C: valid signatures, then single flips ----------------
    Stats sc, scm;
    for (uint32_t it = 0; it < 2500; ++it) {
        uint8_t sk[32], pub[32], sig[64], msg[300];
        rnd_bytes(sk, 32);
        const uint32_t mlen = rnd_below(300);
        rnd_bytes(msg, mlen);
        ed_sign(sig, pub, sk, msg, mlen);
        if (!both(sc, "C-valid", sig, msg, mlen, pub)) {
            if (sc.reject <= 3) { std::printf("FAIL: valid signature rejected\n"); }
            ok = false;
        }
        // One bit flip anywhere must be rejected by both.
        const uint32_t total = 64 + 32 + mlen;
        const uint32_t pos = rnd_below(total);
        const uint8_t bit = static_cast<uint8_t>(1u << rnd_below(8));
        if (pos < 64) { sig[pos] ^= bit; }
        else if (pos < 96) { pub[pos - 64] ^= bit; }
        else { msg[pos - 96] ^= bit; }
        if (both(scm, "C-flip", sig, msg, mlen, pub)) {
            std::printf("FAIL: single-bit-flipped signature accepted\n");
            ok = false;
        }
    }
    report("C valid signer signatures", sc);
    report("C ... with one bit flipped", scm);

    // ---------------- D: structured edge cases ----------------
    Stats sd;
    {
        const char* torsion[8] = {
            "0100000000000000000000000000000000000000000000000000000000000000",
            "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
            "0000000000000000000000000000000000000000000000000000000000000080",
            "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
        };
        uint8_t tk[8][32];
        for (int i = 0; i < 8; ++i) { (void)hex_decode(torsion[i], tk[i], 32); }
        // Special S values.
        uint8_t svals[7][32];
        std::memset(svals, 0, sizeof(svals));
        const uint8_t L[32] = {0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58,
                               0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14,
                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
        std::memcpy(svals[0], L, 32); svals[0][0] = 0xEC;     // L - 1
        std::memcpy(svals[1], L, 32);                          // L
        std::memcpy(svals[2], L, 32); svals[2][0] = 0xEE;     // L + 1
        std::memset(svals[3], 0xFF, 32); svals[3][31] = 0x1F; // 2^253 - 1
        std::memset(svals[4], 0xFF, 32);                       // 2^256 - 1
        svals[5][0] = 1;                                       // 1
        /* svals[6] = 0 */
        for (uint32_t it = 0; it < 4000; ++it) {
            uint8_t pub[32], sig[64], msg[48];
            const uint32_t mlen = rnd_below(48);
            rnd_bytes(msg, mlen);
            rnd_bytes(sig, 64);
            const uint32_t kind = it % 8;
            if (kind == 0) {
                // torsion key, R = random torsion encoding, S = 0
                std::memcpy(pub, tk[rnd_below(8)], 32);
                std::memcpy(sig, tk[rnd_below(8)], 32);
                std::memset(sig + 32, 0, 32);
            } else if (kind == 1) {
                // torsion key with sign bit toggled / y + p non-canonical
                std::memcpy(pub, tk[rnd_below(8)], 32);
                if (rnd() & 1) { pub[31] ^= 0x80; }
                else {
                    // add p to y when y + p < 2^255 (only y < 19 qualifies)
                    uint32_t c = 0;
                    pub[0] = static_cast<uint8_t>(pub[0] + 0xED);
                    c = (pub[0] < 0xED) ? 1u : 0u;
                    for (int i = 1; i < 31; ++i) {
                        const uint32_t t = pub[i] + 0xFFu + c;
                        pub[i] = static_cast<uint8_t>(t);
                        c = t >> 8;
                    }
                    pub[31] = static_cast<uint8_t>(pub[31] + 0x7F + c);
                }
            } else if (kind == 2) {
                // key with y in [p, 2^255): 0xED..0xFF low byte, rest 0xFF, top 0x7F
                std::memset(pub, 0xFF, 32);
                pub[31] = static_cast<uint8_t>(0x7F | ((rnd() & 1) << 7));
                pub[0] = static_cast<uint8_t>(0xED + rnd_below(0x13));
            } else if (kind == 3) {
                // valid signed message with S replaced by a special value
                uint8_t sk[32];
                rnd_bytes(sk, 32);
                ed_sign(sig, pub, sk, msg, mlen);
                std::memcpy(sig + 32, svals[rnd_below(7)], 32);
            } else if (kind == 4) {
                // valid signature, S + L (malleated: non-canonical S)
                uint8_t sk[32];
                rnd_bytes(sk, 32);
                ed_sign(sig, pub, sk, msg, mlen);
                uint32_t c = 0;
                for (int i = 0; i < 32; ++i) {
                    const uint32_t t = sig[32 + i] + L[i] + c;
                    sig[32 + i] = static_cast<uint8_t>(t);
                    c = t >> 8;
                }
            } else if (kind == 5) {
                // mixed-order key: valid A plus a torsion point
                uint8_t sk[32], apub[32];
                rnd_bytes(sk, 32);
                ed_sign(sig, apub, sk, msg, mlen);
                ed::GePoint A, T, S;
                (void)ed::ge_frombytes(A, apub);
                (void)ed::ge_frombytes(T, tk[1 + rnd_below(7)]);
                ed::ge_add(S, A, T);
                ed::ge_tobytes(pub, S);
            } else if (kind == 6) {
                // valid signature with R replaced by a non-canonical / torsion R
                uint8_t sk[32];
                rnd_bytes(sk, 32);
                ed_sign(sig, pub, sk, msg, mlen);
                std::memcpy(sig, tk[rnd_below(8)], 32);
            } else {
                // valid signature, unmodified (accept path, varied keys)
                uint8_t sk[32];
                rnd_bytes(sk, 32);
                ed_sign(sig, pub, sk, msg, mlen);
            }
            (void)both(sd, "D", sig, msg, mlen, pub);
        }
    }
    report("D structured edge cases", sd);

    const uint64_t total = sa.cases + sb.cases + sc.cases + scm.cases + sd.cases;
    const uint64_t mism = sa.mismatch + sb.mismatch + sc.mismatch + scm.mismatch + sd.mismatch;
    const uint64_t acc = sa.accept + sb.accept + sc.accept + scm.accept + sd.accept;
    std::printf("diff_ed25519: %llu cases, %llu accept, %llu reject, %llu C/C++ mismatches\n",
                static_cast<unsigned long long>(total),
                static_cast<unsigned long long>(acc),
                static_cast<unsigned long long>(total - acc),
                static_cast<unsigned long long>(mism));
    if (mism != 0 || total < 20000) { ok = false; }
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
