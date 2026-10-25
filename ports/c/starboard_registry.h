// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// starboard_registry.h — Ed25519 verification + the full registry trust
// pipeline (C99 port of crypto/ed25519_verify.hpp, trust.hpp's
// CapabilityAssertion and registry.hpp). Kept separate from starboard.h so
// the verdict-path header stays crypto-free; everything here is
// verification only, allocation-free and recursion-free.

#ifndef STARBOARD_REGISTRY_H
#define STARBOARD_REGISTRY_H

// starboard.h has no C++ linkage guard of its own; wrap it so a C++
// translation unit (e.g. tests/diff_ed25519.cpp) links the C objects.
#ifdef __cplusplus
extern "C" {
#endif

#include "starboard.h"

// ===========================================================================
// crypto/ed25519_verify.hpp — RFC 8032 Ed25519, verify only (ed25519.c).
// Cofactorless; rejects S >= L, non-canonical / off-curve / all-zero /
// small-order public keys, and x == 0 with the sign bit set. Portable to
// 32-bit cores: no 128-bit types, only 32x32->64 products.
// ===========================================================================
bool sb_ed25519_verify(const uint8_t signature[64], const uint8_t *message,
                       uint64_t message_len, const uint8_t public_key[32]);

// ===========================================================================
// registry.hpp — constants (REGISTRY.md §3, §5, §6).
// ===========================================================================
#define SB_PROOF_MAX_DEPTH      (64U)
#define SB_ASSERTION_BYTES      (53U)
#define SB_SNAPSHOT_BYTES       (167U)
#define SB_REVOCATION_HDR_BYTES (23U)
// Snapshot freshness bound: 30 days in ms (REGISTRY.md §3, step 1a).
#define SB_SNAPSHOT_MAX_AGE_MS  (2592000000ULL)

// A signed capability assertion (trust.hpp CapabilityAssertion; same field
// order and meaning). The signature covers sb_encode_assertion() bytes.
typedef struct {
    ObjectId object_id;
    Tier     tier;
    bool     can_maneuver;
    uint64_t epoch_ms;        // validity window start (GPS ms)
    uint64_t expiry_ms;       // validity window end
    uint8_t  key_id[8];       // operator key fingerprint
    uint8_t  signature[64];   // Ed25519 over the canonical bytes
} CapabilityAssertion;

// A signed registry snapshot (registry.hpp RegistrySnapshot).
typedef struct {
    uint64_t log_size;
    uint8_t  root[SB_HASH_BYTES];             // Merkle root of the entry log
    uint8_t  revocation_root[SB_HASH_BYTES];  // hash of the revocation list
    uint64_t epoch_ms;
    uint8_t  registry_key_id[8];
    uint8_t  signature[64];                   // Ed25519 by the registry key
} RegistrySnapshot;

// Merkle inclusion proof (registry.hpp InclusionProof); siblings bottom-up.
typedef struct {
    uint64_t leaf_index;
    uint32_t depth;
    uint8_t  siblings[SB_PROOF_MAX_DEPTH][SB_HASH_BYTES];
} InclusionProof;

// --- Canonical encodings (REGISTRY.md §5). Return the byte count. ---------
uint32_t sb_encode_assertion(const CapabilityAssertion *a,
                             uint8_t out[SB_ASSERTION_BYTES]);
uint32_t sb_encode_entry(const RegistryEntry *e, uint8_t out[SB_ENTRY_BYTES]);
uint32_t sb_encode_snapshot(const RegistrySnapshot *s,
                            uint8_t out[SB_SNAPSHOT_BYTES]);

// --- Merkle (leaf/node hash and sb_merkle_root are in starboard.h). --------
// Ground-side proof generation; nodes[] (n leaf hashes) is destroyed.
bool sb_merkle_proof(uint8_t (*nodes)[SB_HASH_BYTES], uint32_t n,
                     uint64_t index, InclusionProof *out);
// Flight-side proof verification (bounded, allocation-free).
bool sb_verify_inclusion(const uint8_t leaf[SB_HASH_BYTES],
                         const InclusionProof *p, uint64_t log_size,
                         const uint8_t root[SB_HASH_BYTES]);

// --- Revocation list hash: "STARBOARD:REV/1" || count(8) || ids... ---------
// revoked_key_ids may be NULL when count == 0.
void sb_revocation_hash(const uint8_t (*revoked_key_ids)[8], uint32_t count,
                        uint8_t out[SB_HASH_BYTES]);

// --- The pipeline (SPEC §8.2, REGISTRY.md §6). ------------------------------
// Normative check order: 1 snapshot signature, 1a snapshot freshness
// (epoch <= now <= epoch + max_snapshot_age_ms), 2 revocation list hash and
// membership, 3 entry binding / not_before / inclusion, 4 assertion
// signature and validity window. Any failure returns TRUST_ASSERTED.
// Pass SB_SNAPSHOT_MAX_AGE_MS for the default freshness bound.
TrustLevel sb_establish_trust(const CapabilityAssertion *assertion,
                              const RegistryEntry *entry,
                              const InclusionProof *proof,
                              const RegistrySnapshot *snapshot,
                              const uint8_t (*revoked_key_ids)[8],
                              uint32_t revoked_count,
                              const uint8_t registry_public_key[32],
                              uint64_t now_ms,
                              uint64_t max_snapshot_age_ms);

#ifdef __cplusplus
}
#endif

#endif /* STARBOARD_REGISTRY_H */
