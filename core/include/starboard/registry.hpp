// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — the Registry (SPEC §8.2, docs/REGISTRY.md).
//
// The offline trust chain a craft walks to upgrade a counterpart's assertion
// from ASSERTED to VERIFIED, with no ground contact at decision time:
//
//   capability assertion  --Ed25519-->  operator key
//   operator key binding  --Merkle inclusion proof-->  registry log root
//   log root + revocation list  --Ed25519-->  registry root key (on board)
//
// Everything here is verification: canonical byte encodings (all domain-
// separated), an RFC 6962-compatible Merkle tree over SHA-512, and the
// establish_trust() pipeline. Every check that fails degrades to ASSERTED —
// which Layer 1 treats as "cannot maneuver, OPERATIONAL tier": failure is
// conservative, never fatal (SPEC §8.3).
//
// Revocation is CURRENT-STATE, not log-history: the (small) list of revoked
// operator key ids ships with every snapshot and is pinned by a hash inside
// the signed payload. A revoked operator cannot omit it — omitting it
// changes the hash and kills the snapshot signature.
//
// Allocation-free and recursion-free; proofs are bounded at 64 levels
// (a log of 2^64 entries).
// ============================================================================

#include "crypto/ed25519_verify.hpp"
#include "crypto/sha512.hpp"
#include "trust.hpp"

namespace starboard {
namespace registry {

constexpr uint32_t PROOF_MAX_DEPTH = 64u;
constexpr uint32_t HASH_BYTES = 64u;

// Snapshot freshness bound (REGISTRY.md §6). A validly-signed but STALE
// snapshot carries a stale revocation list; without an age bound an
// adversary could replay a pre-revocation snapshot to un-revoke a key
// (the revocation check would consult the old, shorter list). The default
// is 30 days; operators SHOULD refresh on catalog cadence and MAY tighten
// it. Rollback beyond this bound degrades to ASSERTED, never to trust.
constexpr uint64_t SNAPSHOT_MAX_AGE_MS = 2592000000ull;   // 30 days

// Domain-separation prefixes (15 ASCII bytes each, pinned — REGISTRY.md §5).
// Distinct prefixes mean a signature over one structure can never be
// replayed as a signature over another.
constexpr char DOMAIN_ASSERTION[16] = "STARBOARD:CAP/1";
constexpr char DOMAIN_ENTRY[16]     = "STARBOARD:ENT/1";
constexpr char DOMAIN_SNAPSHOT[16]  = "STARBOARD:REG/1";
constexpr char DOMAIN_REVOCATION[16] = "STARBOARD:REV/1";

// A log entry: binds an operator public key to a craft identity.
struct RegistryEntry {
    ObjectId object_id;
    uint8_t  key_id[8];        // fingerprint referenced by assertions
    uint8_t  public_key[32];   // Ed25519 operator key
    uint64_t not_before_ms;    // binding validity start
};

// A signed snapshot: the registry state a craft carries on board.
struct RegistrySnapshot {
    uint64_t log_size;
    uint8_t  root[HASH_BYTES];             // Merkle root of the entry log
    uint8_t  revocation_root[HASH_BYTES];  // hash of the revocation list
    uint64_t epoch_ms;
    uint8_t  registry_key_id[8];
    uint8_t  signature[64];    // Ed25519 by the registry root key
};

struct InclusionProof {
    uint64_t leaf_index;
    uint32_t depth;
    uint8_t  siblings[PROOF_MAX_DEPTH][HASH_BYTES];  // bottom-up
};

namespace detail {

inline bool bytes_equal(const uint8_t* a, const uint8_t* b, uint32_t n) {
    uint8_t acc = 0u;
    for (uint32_t i = 0u; i < n; ++i) { acc |= (a[i] ^ b[i]); }
    return acc == 0u;
}

inline uint32_t put_u64le(uint8_t* out, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    return 8u;
}

inline uint32_t put_domain(uint8_t* out, const char* domain) {
    for (uint32_t i = 0u; i < 15u; ++i) {
        out[i] = static_cast<uint8_t>(domain[i]);
    }
    return 15u;
}

} // namespace detail

// --- Canonical encodings (packed, little-endian — REGISTRY.md §5) ----------

// "STARBOARD:CAP/1" || id(12) || tier(1) || can_maneuver(1) || epoch(8) ||
// expiry(8) || key_id(8)  = 53 bytes. This is what the operator key signs.
constexpr uint32_t ASSERTION_BYTES = 53u;
inline uint32_t encode_assertion(const CapabilityAssertion& a,
                                 uint8_t out[ASSERTION_BYTES]) {
    uint32_t n = detail::put_domain(out, DOMAIN_ASSERTION);
    for (int i = 0; i < 12; ++i) {
        out[n] = static_cast<uint8_t>(a.object_id.c[i]);
        ++n;
    }
    out[n] = static_cast<uint8_t>(a.tier);
    ++n;
    out[n] = a.can_maneuver ? 1u : 0u;
    ++n;
    n += detail::put_u64le(&out[n], a.epoch_ms);
    n += detail::put_u64le(&out[n], a.expiry_ms);
    for (int i = 0; i < 8; ++i) {
        out[n] = a.key_id[i];
        ++n;
    }
    return n;
}

// "STARBOARD:ENT/1" || id(12) || key_id(8) || pubkey(32) || not_before(8)
// = 75 bytes. Hashed (not signed) — entries gain authority via the log root.
constexpr uint32_t ENTRY_BYTES = 75u;
inline uint32_t encode_entry(const RegistryEntry& e,
                             uint8_t out[ENTRY_BYTES]) {
    uint32_t n = detail::put_domain(out, DOMAIN_ENTRY);
    for (int i = 0; i < 12; ++i) {
        out[n] = static_cast<uint8_t>(e.object_id.c[i]);
        ++n;
    }
    for (int i = 0; i < 8; ++i) {
        out[n] = e.key_id[i];
        ++n;
    }
    for (int i = 0; i < 32; ++i) {
        out[n] = e.public_key[i];
        ++n;
    }
    n += detail::put_u64le(&out[n], e.not_before_ms);
    return n;
}

// "STARBOARD:REG/1" || log_size(8) || root(64) || revocation_root(64) ||
// epoch(8) || key_id(8) = 167 bytes. This is what the registry key signs.
constexpr uint32_t SNAPSHOT_BYTES = 167u;
inline uint32_t encode_snapshot(const RegistrySnapshot& s,
                                uint8_t out[SNAPSHOT_BYTES]) {
    uint32_t n = detail::put_domain(out, DOMAIN_SNAPSHOT);
    n += detail::put_u64le(&out[n], s.log_size);
    for (uint32_t i = 0u; i < HASH_BYTES; ++i) {
        out[n] = s.root[i];
        ++n;
    }
    for (uint32_t i = 0u; i < HASH_BYTES; ++i) {
        out[n] = s.revocation_root[i];
        ++n;
    }
    n += detail::put_u64le(&out[n], s.epoch_ms);
    for (int i = 0; i < 8; ++i) {
        out[n] = s.registry_key_id[i];
        ++n;
    }
    return n;
}

// --- Merkle tree (RFC 6962 conventions over SHA-512) -------------------------

inline void leaf_hash(const RegistryEntry& e, uint8_t out[HASH_BYTES]) {
    uint8_t buf[1u + ENTRY_BYTES];
    buf[0] = 0x00u;                       // RFC 6962 leaf prefix
    (void)encode_entry(e, &buf[1]);
    crypto::sha512(buf, 1u + ENTRY_BYTES, out);
}

inline void node_hash(const uint8_t left[HASH_BYTES],
                      const uint8_t right[HASH_BYTES],
                      uint8_t out[HASH_BYTES]) {
    uint8_t buf[1u + 2u * HASH_BYTES];
    buf[0] = 0x01u;                       // RFC 6962 interior prefix
    for (uint32_t i = 0u; i < HASH_BYTES; ++i) {
        buf[1u + i] = left[i];
        buf[1u + HASH_BYTES + i] = right[i];
    }
    uint8_t tmp[HASH_BYTES];
    crypto::sha512(buf, sizeof(buf), tmp);
    for (uint32_t i = 0u; i < HASH_BYTES; ++i) { out[i] = tmp[i]; }
}

// Compute the root over n leaf hashes IN PLACE (nodes[] is destroyed).
// Layer-by-layer with odd-node promotion — identical to the RFC 6962
// recursive definition for every n.
inline void merkle_root(uint8_t (*nodes)[HASH_BYTES], uint32_t n,
                        uint8_t out[HASH_BYTES]) {
    if (n == 0u) {
        crypto::sha512(nullptr, 0u, out);   // MTH({}) = SHA-512("")
        return;
    }
    while (n > 1u) {
        uint32_t w = 0u;
        for (uint32_t i = 0u; (i + 1u) < n; i += 2u) {
            node_hash(nodes[i], nodes[i + 1u], nodes[w]);
            ++w;
        }
        if ((n & 1u) != 0u) {
            for (uint32_t j = 0u; j < HASH_BYTES; ++j) {
                nodes[w][j] = nodes[n - 1u][j];
            }
            ++w;
        }
        n = w;
    }
    for (uint32_t j = 0u; j < HASH_BYTES; ++j) { out[j] = nodes[0][j]; }
}

// Generate the inclusion proof for leaf `index` (nodes[] is destroyed).
// Ground-side tooling; craft only verify.
inline bool merkle_proof(uint8_t (*nodes)[HASH_BYTES], uint32_t n,
                         uint64_t index, InclusionProof* out) {
    if ((index >= n) || (n == 0u)) { return false; }
    out->leaf_index = index;
    out->depth = 0u;
    while (n > 1u) {
        const uint64_t sib = index ^ 1ull;
        if (sib < n) {
            if (out->depth >= PROOF_MAX_DEPTH) { return false; }
            for (uint32_t j = 0u; j < HASH_BYTES; ++j) {
                out->siblings[out->depth][j] = nodes[sib][j];
            }
            ++out->depth;
        }
        uint32_t w = 0u;
        for (uint32_t i = 0u; (i + 1u) < n; i += 2u) {
            node_hash(nodes[i], nodes[i + 1u], nodes[w]);
            ++w;
        }
        if ((n & 1u) != 0u) {
            for (uint32_t j = 0u; j < HASH_BYTES; ++j) {
                nodes[w][j] = nodes[n - 1u][j];
            }
            ++w;
        }
        n = w;
        index >>= 1u;
    }
    return true;
}

// Verify an inclusion proof against a root (flight path: allocation-free,
// bounded work).
inline bool verify_inclusion(const uint8_t leaf[HASH_BYTES],
                             const InclusionProof& p, uint64_t log_size,
                             const uint8_t root[HASH_BYTES]) {
    if ((log_size == 0ull) || (p.leaf_index >= log_size) ||
        (p.depth > PROOF_MAX_DEPTH)) {
        return false;
    }
    uint8_t cur[HASH_BYTES];
    for (uint32_t j = 0u; j < HASH_BYTES; ++j) { cur[j] = leaf[j]; }
    uint64_t idx = p.leaf_index;
    uint64_t n = log_size;
    uint32_t used = 0u;
    while (n > 1ull) {
        const uint64_t sib = idx ^ 1ull;
        if (sib < n) {
            if (used >= p.depth) { return false; }
            if ((idx & 1ull) != 0ull) {
                node_hash(p.siblings[used], cur, cur);
            } else {
                node_hash(cur, p.siblings[used], cur);
            }
            ++used;
        }
        idx >>= 1u;
        n = (n + 1ull) >> 1u;
    }
    return (used == p.depth) && detail::bytes_equal(cur, root, HASH_BYTES);
}

// --- Revocation list ---------------------------------------------------------
// Hash of the full current revocation list: "STARBOARD:REV/1" || count(8) ||
// key_id(8) * count, in the exact shipped order. The list rides with the
// snapshot; the hash inside the signed payload pins it.
inline void revocation_hash(const uint8_t (*revoked_key_ids)[8],
                            uint32_t count, uint8_t out[HASH_BYTES]) {
    crypto::Sha512State st;
    crypto::sha512_init(st);
    uint8_t hdr[23];
    uint32_t n = detail::put_domain(hdr, DOMAIN_REVOCATION);
    n += detail::put_u64le(&hdr[n], static_cast<uint64_t>(count));
    crypto::sha512_update(st, hdr, n);
    for (uint32_t i = 0u; i < count; ++i) {
        crypto::sha512_update(st, revoked_key_ids[i], 8u);
    }
    crypto::sha512_final(st, out);
}

// --- The pipeline (SPEC §8.2) -------------------------------------------------
// Establish the trust level of a counterpart's assertion, fully offline.
// Any failed check returns ASSERTED — conservative, never fatal: Layer 1
// then treats the craft as debris (SPEC §8.3, Table 8-1).
inline TrustLevel establish_trust(const CapabilityAssertion& assertion,
                                  const RegistryEntry& entry,
                                  const InclusionProof& proof,
                                  const RegistrySnapshot& snapshot,
                                  const uint8_t (*revoked_key_ids)[8],
                                  uint32_t revoked_count,
                                  const uint8_t registry_public_key[32],
                                  uint64_t now_ms,
                                  uint64_t max_snapshot_age_ms =
                                      SNAPSHOT_MAX_AGE_MS) {
    // 1. The snapshot is authentic (signed by the on-board registry key).
    uint8_t snap_payload[SNAPSHOT_BYTES];
    const uint32_t sn = encode_snapshot(snapshot, snap_payload);
    if (!crypto::ed25519_verify(snapshot.signature, snap_payload, sn,
                                registry_public_key)) {
        return TrustLevel::ASSERTED;
    }
    // 1a. The snapshot is FRESH: not from the future (clock skew / rollback)
    //     and not older than the freshness bound. A stale snapshot carries a
    //     stale revocation list, so this is what makes revocation binding.
    if (now_ms < snapshot.epoch_ms) { return TrustLevel::ASSERTED; }
    if ((now_ms - snapshot.epoch_ms) > max_snapshot_age_ms) {
        return TrustLevel::ASSERTED;
    }
    // 2. The shipped revocation list is the one the snapshot pinned, and the
    //    asserting key is not on it.
    uint8_t rev_h[HASH_BYTES];
    revocation_hash(revoked_key_ids, revoked_count, rev_h);
    if (!detail::bytes_equal(rev_h, snapshot.revocation_root, HASH_BYTES)) {
        return TrustLevel::ASSERTED;
    }
    for (uint32_t i = 0u; i < revoked_count; ++i) {
        if (detail::bytes_equal(revoked_key_ids[i], assertion.key_id, 8u)) {
            return TrustLevel::ASSERTED;
        }
    }
    // 3. The entry binds THIS craft to THIS key, is in force, and is in the
    //    log the snapshot signed.
    if (!id_equal(entry.object_id, assertion.object_id)) {
        return TrustLevel::ASSERTED;
    }
    if (!detail::bytes_equal(entry.key_id, assertion.key_id, 8u)) {
        return TrustLevel::ASSERTED;
    }
    if (now_ms < entry.not_before_ms) { return TrustLevel::ASSERTED; }
    uint8_t lh[HASH_BYTES];
    leaf_hash(entry, lh);
    if (!verify_inclusion(lh, proof, snapshot.log_size, snapshot.root)) {
        return TrustLevel::ASSERTED;
    }
    // 4. The assertion is signed by the bound key and inside its window.
    uint8_t ap[ASSERTION_BYTES];
    const uint32_t an = encode_assertion(assertion, ap);
    if (!crypto::ed25519_verify(assertion.signature, ap, an,
                                entry.public_key)) {
        return TrustLevel::ASSERTED;
    }
    if ((now_ms < assertion.epoch_ms) || (now_ms > assertion.expiry_ms)) {
        return TrustLevel::ASSERTED;
    }
    return TrustLevel::VERIFIED;
}

} // namespace registry
} // namespace starboard
