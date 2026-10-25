// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// test_crypto.c — standalone test program for the C99 port's crypto and
// registry trust pipeline. Mirrors conformance/runner.cpp SB-015..SB-019
// (same vectors, same fixtures — registry_fixtures_c.h is mechanically
// transcribed from conformance/registry_fixtures.hpp) plus extra rejection
// cases. Host-side test tooling, not flight code (MISRA deviation: stdio).
//
// Exit status 0 iff every check passes.

/* The SHA-512 / Ed25519 vectors below are intentionally split across lines
 * by string-literal concatenation; clang's heuristic otherwise flags them. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wstring-concatenation"
#endif
#include <stdio.h>
#include <string.h>

#include "starboard_registry.h"
#include "registry_fixtures_c.h"

static uint32_t g_pass = 0u;
static uint32_t g_fail = 0u;

static void check(const char *group, const char *name, bool ok)
{
    if (ok)
    {
        ++g_pass;
    }
    else
    {
        ++g_fail;
        (void)printf("  FAIL  %s: %s\n", group, name);
    }
}

static uint8_t nib(char c)
{
    uint8_t v;
    if ((c >= '0') && (c <= '9'))
    {
        v = (uint8_t)(c - '0');
    }
    else if ((c >= 'a') && (c <= 'f'))
    {
        v = (uint8_t)((c - 'a') + 10);
    }
    else
    {
        v = (uint8_t)((c - 'A') + 10);
    }
    return v;
}

// Decode a hex string (concatenated literal) into out (capacity cap).
static uint32_t hex_decode(const char *hex, uint8_t *out, uint32_t cap)
{
    uint32_t n = 0u;
    while ((hex[2u * n] != '\0') && (n < cap))
    {
        out[n] = (uint8_t)((uint32_t)(nib(hex[2u * n]) << 4) |
                           (uint32_t)nib(hex[(2u * n) + 1u]));
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// SB-015  SHA-512 (FIPS 180-2 vectors + the one-million-'a' vector).
// ---------------------------------------------------------------------------
static void test_sha512(void)
{
    static const char *const msgs[3] = {
        "abc",
        "",
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
    };
    static const char *const digests[3] = {
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
        "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
        "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"
    };
    uint8_t want[64];
    uint8_t got[64];
    uint32_t i;
    for (i = 0u; i < 3u; ++i)
    {
        (void)hex_decode(digests[i], want, 64u);
        sb_sha512((const uint8_t *)msgs[i], (uint64_t)strlen(msgs[i]), got);
        check("SB-015 SHA-512", (i == 0u) ? "abc" : ((i == 1u) ? "empty" : "896-bit"),
              memcmp(want, got, 64u) == 0);
    }
    // One million 'a', streamed in odd-sized chunks (exercises buffering).
    {
        Sha512State st;
        uint8_t chunk[997];
        uint32_t left = 1000000u;
        (void)memset(chunk, 'a', sizeof(chunk));
        sb_sha512_init(&st);
        while (left > 0u)
        {
            const uint32_t n = (left < 997u) ? left : 997u;
            sb_sha512_update(&st, chunk, n);
            left -= n;
        }
        sb_sha512_final(&st, got);
        (void)hex_decode(
            "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
            "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b",
            want, 64u);
        check("SB-015 SHA-512", "1,000,000 x 'a'", memcmp(want, got, 64u) == 0);
    }
}

// ---------------------------------------------------------------------------
// SB-016  Ed25519: RFC 8032 §7.1 TEST 1, 2, 3 and TEST 1024, plus the
//         runner.cpp rejection cases and extra strictness cases.
// ---------------------------------------------------------------------------

// RFC 8032 §7.1 TEST 1024 (1023-byte message). Transcribed from the SUPERCOP
// sign.input set (entry 1024; secret key f5e5767c...0ee5), the source of the
// RFC's §7.1 vectors.
static const char T1024_PUB[] =
    "278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e";
static const char T1024_SIG[] =
    "0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350"
    "aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03";
static const char T1024_MSG[] =
    "08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98"
    "fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d8"
    "79de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d"
    "658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc"
    "1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4fe"
    "ba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e"
    "06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbef"
    "efd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7"
    "aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed1"
    "85ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2"
    "d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24"
    "554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f270"
    "88d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc"
    "2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b07"
    "07e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128ba"
    "b27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51a"
    "ddd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429e"
    "c96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb7"
    "51fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c"
    "42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8"
    "ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34df"
    "f7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08"
    "d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649"
    "de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e4"
    "88acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a3"
    "2ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e"
    "6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5f"
    "b93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b5"
    "0d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1"
    "369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380d"
    "b2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c"
    "0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0";

static void test_ed25519(void)
{
    static const char *const pubs[3] = {
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025"
    };
    static const char *const msgs[3] = { "", "72", "af82" };
    static const char *const sigs[3] = {
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
        "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
    };
    static const char *const names[3] = { "TEST 1", "TEST 2", "TEST 3" };
    const char *grp = "SB-016 Ed25519";
    uint8_t pub[32];
    uint8_t sig[64];
    uint8_t msg[1024];
    uint8_t bad[64];
    uint8_t badpub[32];
    uint32_t mlen;
    uint32_t i;

    for (i = 0u; i < 3u; ++i)
    {
        (void)hex_decode(pubs[i], pub, 32u);
        (void)hex_decode(sigs[i], sig, 64u);
        mlen = hex_decode(msgs[i], msg, 8u);
        check(grp, names[i], sb_ed25519_verify(sig, msg, mlen, pub));
        (void)memcpy(bad, sig, 64u);
        bad[0] ^= 1u;
        check(grp, "R bit flip rejected", !sb_ed25519_verify(bad, msg, mlen, pub));
        (void)memcpy(bad, sig, 64u);
        bad[63] ^= 0x40u;
        check(grp, "S high bit flip rejected", !sb_ed25519_verify(bad, msg, mlen, pub));
        (void)memcpy(badpub, pub, 32u);
        badpub[7] ^= 4u;
        check(grp, "public key bit flip rejected", !sb_ed25519_verify(sig, msg, mlen, badpub));
    }
    // TEST 1 with a NULL message pointer and zero length.
    (void)hex_decode(pubs[0], pub, 32u);
    (void)hex_decode(sigs[0], sig, 64u);
    check(grp, "TEST 1 (NULL message, len 0)", sb_ed25519_verify(sig, NULL, 0u, pub));

    // TEST 1024.
    (void)hex_decode(T1024_PUB, pub, 32u);
    (void)hex_decode(T1024_SIG, sig, 64u);
    mlen = hex_decode(T1024_MSG, msg, sizeof(msg));
    check(grp, "TEST 1024 message length == 1023", mlen == 1023u);
    check(grp, "TEST 1024", sb_ed25519_verify(sig, msg, mlen, pub));
    msg[1022] ^= 0x80u;
    check(grp, "TEST 1024 last-byte flip rejected", !sb_ed25519_verify(sig, msg, mlen, pub));
    msg[1022] ^= 0x80u;
    check(grp, "TEST 1024 truncated rejected", !sb_ed25519_verify(sig, msg, mlen - 1u, pub));

    // Tampered message (runner.cpp).
    {
        const uint8_t wrong[2] = { 0xAFu, 0x83u };
        (void)hex_decode(pubs[2], pub, 32u);
        (void)hex_decode(sigs[2], sig, 64u);
        check(grp, "tampered message rejected", !sb_ed25519_verify(sig, wrong, 2u, pub));
    }
    // All-zero key, identity-key forgery, S == L (runner.cpp).
    {
        const uint8_t zero[32] = { 0u };
        const uint8_t zsig[64] = { 0u };
        uint8_t ident[32] = { 0u };
        uint8_t forge[64] = { 0u };
        uint8_t sl[64] = { 0u };
        static const uint8_t lbytes[32] = {
            0xEDu, 0xD3u, 0xF5u, 0x5Cu, 0x1Au, 0x63u, 0x12u, 0x58u,
            0xD6u, 0x9Cu, 0xF7u, 0xA2u, 0xDEu, 0xF9u, 0xDEu, 0x14u,
            0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x10u
        };
        check(grp, "all-zero public key rejected", !sb_ed25519_verify(zsig, NULL, 0u, zero));
        ident[0] = 1u;
        forge[0] = 1u;
        check(grp, "identity-key forgery rejected", !sb_ed25519_verify(forge, NULL, 0u, ident));
        (void)hex_decode(pubs[0], pub, 32u);
        (void)memcpy(&sl[32], lbytes, 32u);
        check(grp, "S == L rejected", !sb_ed25519_verify(sl, NULL, 0u, pub));
        // TEST 1 with S + L (the classic malleation) must be rejected.
        {
            uint32_t carry = 0u;
            (void)hex_decode(sigs[0], sig, 64u);
            for (i = 0u; i < 32u; ++i)
            {
                const uint32_t s = (uint32_t)sig[32u + i] + (uint32_t)lbytes[i] + carry;
                sig[32u + i] = (uint8_t)s;
                carry = s >> 8;
            }
            check(grp, "TEST 1 with S + L rejected", !sb_ed25519_verify(sig, NULL, 0u, pub));
        }
    }
    // Small-order keys: each (R, S = 0, msg) below is a VALID signature under
    // plain cofactorless RFC 8032 (found by search in Python); Starboard must
    // reject all of them because [8]A == identity.
    {
        static const char *const forg[7][3] = {
            { "0100000000000000000000000000000000000000000000000000000000000000",
              "0100000000000000000000000000000000000000000000000000000000000000",
              "starboard-forge-0" },
            { "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
              "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
              "starboard-forge-0" },
            { "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
              "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
              "starboard-forge-1" },
            { "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
              "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
              "starboard-forge-0" },
            { "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
              "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
              "starboard-forge-2" },
            { "0000000000000000000000000000000000000000000000000000000000000080",
              "0000000000000000000000000000000000000000000000000000000000000000",
              "starboard-forge-1" },
            { "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
              "0000000000000000000000000000000000000000000000000000000000000000",
              "starboard-forge-2" }
        };
        for (i = 0u; i < 7u; ++i)
        {
            (void)memset(sig, 0, 64u);
            (void)hex_decode(forg[i][0], pub, 32u);
            (void)hex_decode(forg[i][1], sig, 32u);
            check(grp, "small-order key forgery rejected",
                  !sb_ed25519_verify(sig, (const uint8_t *)forg[i][2],
                                     (uint64_t)strlen(forg[i][2]), pub));
        }
    }
    // Decoding strictness: x == 0 with sign bit, non-canonical y (>= p),
    // off-curve y.
    {
        static const char *const badkeys[4] = {
            // identity (y = 1) with sign bit set
            "0100000000000000000000000000000000000000000000000000000000000080",
            // y = p (non-canonical encoding of y = 0)
            "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            // y = p + 1 (non-canonical encoding of the identity)
            "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            // y = 2: u/v = 3/(4d + 1) is a non-square, so off-curve
            "0200000000000000000000000000000000000000000000000000000000000000"
        };
        (void)hex_decode(sigs[0], sig, 64u);
        for (i = 0u; i < 4u; ++i)
        {
            (void)hex_decode(badkeys[i], pub, 32u);
            check(grp, "malformed public key rejected", !sb_ed25519_verify(sig, NULL, 0u, pub));
        }
    }
}

// ---------------------------------------------------------------------------
// Encodings (REGISTRY.md §5) — sizes and fixed byte positions.
// ---------------------------------------------------------------------------
static void test_encodings(void)
{
    uint8_t a[SB_ASSERTION_BYTES];
    uint8_t e[SB_ENTRY_BYTES];
    uint8_t s[SB_SNAPSHOT_BYTES];
    const char *grp = "encodings";
    check(grp, "assertion is 53 bytes", sb_encode_assertion(&FX_ASSERT_OK, a) == 53u);
    check(grp, "entry is 75 bytes", sb_encode_entry(&FX_ENTRIES[0], e) == 75u);
    check(grp, "snapshot is 167 bytes", sb_encode_snapshot(&FX_SNAPSHOT, s) == 167u);
    check(grp, "assertion prefix", memcmp(a, "STARBOARD:CAP/1", 15u) == 0);
    check(grp, "entry prefix", memcmp(e, "STARBOARD:ENT/1", 15u) == 0);
    check(grp, "snapshot prefix", memcmp(s, "STARBOARD:REG/1", 15u) == 0);
    check(grp, "assertion tier/can_maneuver bytes", (a[27] == 1u) && (a[28] == 1u));
    check(grp, "assertion key_id at offset 45", memcmp(&a[45], FX_ASSERT_OK.key_id, 8u) == 0);
    check(grp, "snapshot key_id at offset 159",
          memcmp(&s[159], FX_SNAPSHOT.registry_key_id, 8u) == 0);
    // The fixture snapshot signature verifies over our encoding (the C
    // encoder agrees byte-for-byte with the independent Python encoder).
    check(grp, "snapshot signature verifies over C encoding",
          sb_ed25519_verify(FX_SNAPSHOT.signature, s, 167u, FX_REG_PUB));
    check(grp, "assertion signature verifies over C encoding",
          sb_ed25519_verify(FX_ASSERT_OK.signature, a, 53u, FX_ENTRIES[0].public_key));
    {
        uint8_t h[SB_HASH_BYTES];
        sb_revocation_hash(FX_REVOKED_IDS, 1u, h);
        check(grp, "revocation hash == pinned revocation_root",
              memcmp(h, FX_SNAPSHOT.revocation_root, SB_HASH_BYTES) == 0);
        {
            uint8_t h0[SB_HASH_BYTES];
            sb_revocation_hash(NULL, 0u, h0);
            check(grp, "empty revocation list hash differs", memcmp(h, h0, SB_HASH_BYTES) != 0);
        }
    }
}

// ---------------------------------------------------------------------------
// SB-018  Merkle log: root reproduces the snapshot root; proofs verify;
//         corruption fails.
// ---------------------------------------------------------------------------
static void test_merkle(void)
{
    const char *grp = "SB-018 Merkle";
    uint8_t leaves[5][SB_HASH_BYTES];
    uint8_t nodes[5][SB_HASH_BYTES];
    uint8_t root[SB_HASH_BYTES];
    InclusionProof p;
    uint32_t i;

    for (i = 0u; i < 5u; ++i)
    {
        sb_leaf_hash(&FX_ENTRIES[i], leaves[i]);
    }
    (void)memcpy(nodes, leaves, sizeof(nodes));
    sb_merkle_root(nodes, 5u, root);
    check(grp, "cross-impl root", memcmp(root, FX_SNAPSHOT.root, SB_HASH_BYTES) == 0);

    for (i = 0u; i < 5u; ++i)
    {
        (void)memcpy(nodes, leaves, sizeof(nodes));
        (void)memset(&p, 0, sizeof(p));
        if (!sb_merkle_proof(nodes, 5u, i, &p))
        {
            check(grp, "merkle_proof generated", false);
            continue;
        }
        check(grp, "generated proof depth == fixture depth", p.depth == FX_PROOFS[i].depth);
        check(grp, "generated proof == fixture proof",
              memcmp(p.siblings, FX_PROOFS[i].siblings,
                     (size_t)p.depth * SB_HASH_BYTES) == 0);
        check(grp, "generated proof verifies", sb_verify_inclusion(leaves[i], &p, 5u, root));
        check(grp, "fixture proof verifies",
              sb_verify_inclusion(leaves[i], &FX_PROOFS[i], 5u, FX_SNAPSHOT.root));
        if (p.depth > 0u)
        {
            p.siblings[0][0] ^= 1u;
            check(grp, "corrupted proof fails", !sb_verify_inclusion(leaves[i], &p, 5u, root));
            p.siblings[0][0] ^= 1u;
        }
        check(grp, "wrong root fails",
              !sb_verify_inclusion(leaves[i], &FX_PROOFS[i], 5u, FX_SNAPSHOT.revocation_root));
        // log_size 3 changes every path. (log_size 6 would NOT: for a 5-leaf
        // audit path the 6-leaf fold gives the same root — an RFC 6962
        // property, harmless because log_size and root are both signed.)
        check(grp, "wrong log size fails",
              !sb_verify_inclusion(leaves[i], &FX_PROOFS[i], 3u, FX_SNAPSHOT.root));
        {
            InclusionProof q = FX_PROOFS[i];
            q.leaf_index = (uint64_t)((i + 1u) % 5u);
            check(grp, "wrong leaf index fails",
                  !sb_verify_inclusion(leaves[i], &q, 5u, FX_SNAPSHOT.root));
            q = FX_PROOFS[i];
            q.depth = SB_PROOF_MAX_DEPTH + 1u;
            check(grp, "depth > PROOF_MAX_DEPTH fails",
                  !sb_verify_inclusion(leaves[i], &q, 5u, FX_SNAPSHOT.root));
        }
    }
    (void)memcpy(nodes, leaves, sizeof(nodes));
    check(grp, "proof for index >= n refused", !sb_merkle_proof(nodes, 5u, 5u, &p));
    check(grp, "proof over empty log refused", !sb_merkle_proof(nodes, 0u, 0u, &p));
    {
        uint8_t one[1][SB_HASH_BYTES];
        (void)memcpy(one[0], leaves[3], SB_HASH_BYTES);
        check(grp, "single-leaf proof", sb_merkle_proof(one, 1u, 0u, &p) && (p.depth == 0u));
        check(grp, "single-leaf proof verifies",
              sb_verify_inclusion(leaves[3], &p, 1u, leaves[3]));
    }
}

// ---------------------------------------------------------------------------
// SB-017  registry pipeline; SB-019 snapshot freshness.
// ---------------------------------------------------------------------------
static TrustLevel trust_with(const CapabilityAssertion *a, uint32_t entry_i,
                             const uint8_t reg_pub[32], uint64_t now,
                             uint64_t max_age)
{
    return sb_establish_trust(a, &FX_ENTRIES[entry_i], &FX_PROOFS[entry_i],
                              &FX_SNAPSHOT, FX_REVOKED_IDS, 1u, reg_pub, now,
                              max_age);
}

static void test_pipeline(void)
{
    const char *grp = "SB-017 pipeline";
    const uint64_t day = 86400000ULL;
    const uint64_t age = SB_SNAPSHOT_MAX_AGE_MS;

    check(grp, "valid assertion VERIFIED",
          trust_with(&FX_ASSERT_OK, 0u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_VERIFIED);
    check(grp, "revoked key ASSERTED",
          trust_with(&FX_ASSERT_REVOKED, 2u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    check(grp, "expired assertion ASSERTED",
          trust_with(&FX_ASSERT_EXPIRED, 1u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    {
        CapabilityAssertion t = FX_ASSERT_OK;
        t.can_maneuver = !t.can_maneuver;
        check(grp, "tampered assertion ASSERTED",
              trust_with(&t, 0u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
        t = FX_ASSERT_OK;
        t.tier = TIER_CREWED;
        check(grp, "tier-upgraded assertion ASSERTED",
              trust_with(&t, 0u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    }
    {
        uint8_t bad_reg[32];
        (void)memcpy(bad_reg, FX_REG_PUB, 32u);
        bad_reg[3] ^= 8u;
        check(grp, "wrong registry key ASSERTED",
              trust_with(&FX_ASSERT_OK, 0u, bad_reg, FX_NOW_MS, age) == TRUST_ASSERTED);
    }
    check(grp, "wrong entry ASSERTED",
          sb_establish_trust(&FX_ASSERT_OK, &FX_ENTRIES[1], &FX_PROOFS[1], &FX_SNAPSHOT,
                             FX_REVOKED_IDS, 1u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    check(grp, "right entry, wrong proof ASSERTED",
          sb_establish_trust(&FX_ASSERT_OK, &FX_ENTRIES[0], &FX_PROOFS[1], &FX_SNAPSHOT,
                             FX_REVOKED_IDS, 1u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    // Stripping the revocation list kills the pinned hash -> ASSERTED, even
    // for the revoked operator (and for an honest one).
    check(grp, "stripped revocation list ASSERTED (revoked key)",
          sb_establish_trust(&FX_ASSERT_REVOKED, &FX_ENTRIES[2], &FX_PROOFS[2], &FX_SNAPSHOT,
                             NULL, 0u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    check(grp, "stripped revocation list ASSERTED (honest key)",
          sb_establish_trust(&FX_ASSERT_OK, &FX_ENTRIES[0], &FX_PROOFS[0], &FX_SNAPSHOT,
                             NULL, 0u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    {
        RegistrySnapshot s = FX_SNAPSHOT;
        s.epoch_ms += 1u;
        check(grp, "re-dated snapshot ASSERTED",
              sb_establish_trust(&FX_ASSERT_OK, &FX_ENTRIES[0], &FX_PROOFS[0], &s,
                                 FX_REVOKED_IDS, 1u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    }
    {
        RegistryEntry e = FX_ENTRIES[0];
        e.not_before_ms = FX_NOW_MS + 1u;
        check(grp, "entry not yet in force ASSERTED",
              sb_establish_trust(&FX_ASSERT_OK, &e, &FX_PROOFS[0], &FX_SNAPSHOT,
                                 FX_REVOKED_IDS, 1u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_ASSERTED);
    }
    check(grp, "assertion before its epoch ASSERTED",
          trust_with(&FX_ASSERT_OK, 0u, FX_REG_PUB, FX_ASSERT_OK.epoch_ms - 1u, age) ==
              TRUST_ASSERTED);
    check(grp, "assertion at expiry still VERIFIED",
          trust_with(&FX_ASSERT_OK, 0u, FX_REG_PUB, FX_ASSERT_OK.expiry_ms, age) ==
              TRUST_VERIFIED);
    check(grp, "assertion 1 ms past expiry ASSERTED",
          trust_with(&FX_ASSERT_OK, 0u, FX_REG_PUB, FX_ASSERT_OK.expiry_ms + 1u, age) ==
              TRUST_ASSERTED);

    grp = "SB-019 freshness";
    check(grp, "fresh snapshot VERIFIED",
          trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_NOW_MS, age) == TRUST_VERIFIED);
    check(grp, "stale snapshot (400 d) ASSERTED",
          trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_SNAPSHOT_EPOCH_MS + (400u * day), age) ==
              TRUST_ASSERTED);
    check(grp, "future-dated snapshot ASSERTED",
          trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_SNAPSHOT_EPOCH_MS - day, age) ==
              TRUST_ASSERTED);
    check(grp, "exactly at bound VERIFIED",
          trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_SNAPSHOT_EPOCH_MS + age, age) ==
              TRUST_VERIFIED);
    check(grp, "1 ms past bound ASSERTED",
          trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_SNAPSHOT_EPOCH_MS + age + 1u, age) ==
              TRUST_ASSERTED);
    check(grp, "tightened bound (1 h) rejects 1-day-old snapshot",
          trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_NOW_MS, 3600000u) == TRUST_ASSERTED);
    check(grp, "max_age 0 accepts only now == epoch",
          (trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_SNAPSHOT_EPOCH_MS, 0u) ==
           TRUST_VERIFIED) &&
          (trust_with(&FX_ASSERT_LONG, 0u, FX_REG_PUB, FX_SNAPSHOT_EPOCH_MS + 1u, 0u) ==
           TRUST_ASSERTED));
}

int main(void)
{
    test_sha512();
    test_ed25519();
    test_encodings();
    test_merkle();
    test_pipeline();
    (void)printf("test_crypto: %u passed, %u failed (sizeof(void*)=%u)\n",
                 (unsigned)g_pass, (unsigned)g_fail, (unsigned)sizeof(void *));
    return (g_fail == 0u) ? 0 : 1;
}
