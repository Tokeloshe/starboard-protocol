// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// registry.c — the registry (C99 port of registry.hpp, REGISTRY.md):
// canonical encodings, RFC 6962 Merkle tree over SHA-512 (leaf/node hash,
// iterative in-place root, proof generation and verification), the
// revocation-list hash, and the sb_establish_trust() pipeline.
// Allocation-free, recursion-free; proofs bounded at SB_PROOF_MAX_DEPTH.

#include "starboard_registry.h"

// Domain-separation prefixes (15 ASCII bytes each, pinned — REGISTRY.md §5).
static const char SB_DOMAIN_ASSERTION[16]  = "STARBOARD:CAP/1";
static const char SB_DOMAIN_ENTRY[16]      = "STARBOARD:ENT/1";
static const char SB_DOMAIN_SNAPSHOT[16]   = "STARBOARD:REG/1";
static const char SB_DOMAIN_REVOCATION[16] = "STARBOARD:REV/1";

static bool sb_bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0U;
    uint32_t i;
    for (i = 0U; i < n; ++i)
    {
        acc = (uint8_t)(acc | (uint8_t)(a[i] ^ b[i]));
    }
    return acc == 0U;
}

static uint32_t sb_put_u64le(uint8_t *out, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i)
    {
        out[i] = (uint8_t)(v >> (8 * i));
    }
    return 8U;
}

static uint32_t sb_put_domain(uint8_t *out, const char *domain)
{
    uint32_t i;
    for (i = 0U; i < 15U; ++i)
    {
        out[i] = (uint8_t)domain[i];
    }
    return 15U;
}

/* "STARBOARD:CAP/1" || id(12) || tier(1) || can_maneuver(1) || epoch(8) ||
   expiry(8) || key_id(8) = 53 bytes. This is what the operator key signs. */
uint32_t sb_encode_assertion(const CapabilityAssertion *a,
                             uint8_t out[SB_ASSERTION_BYTES])
{
    uint32_t n = sb_put_domain(out, SB_DOMAIN_ASSERTION);
    int i;
    for (i = 0; i < 12; ++i)
    {
        out[n] = (uint8_t)a->object_id.c[i];
        ++n;
    }
    out[n] = (uint8_t)a->tier;
    ++n;
    out[n] = a->can_maneuver ? 1U : 0U;
    ++n;
    n += sb_put_u64le(&out[n], a->epoch_ms);
    n += sb_put_u64le(&out[n], a->expiry_ms);
    for (i = 0; i < 8; ++i)
    {
        out[n] = a->key_id[i];
        ++n;
    }
    return n;
}

/* "STARBOARD:ENT/1" || id(12) || key_id(8) || pubkey(32) || not_before(8)
   = 75 bytes. Hashed (not signed) — entries gain authority via the root. */
uint32_t sb_encode_entry(const RegistryEntry *e,
                                uint8_t out[SB_ENTRY_BYTES])
{
    uint32_t n = sb_put_domain(out, SB_DOMAIN_ENTRY);
    int i;
    for (i = 0; i < 12; ++i)
    {
        out[n] = (uint8_t)e->object_id.c[i];
        ++n;
    }
    for (i = 0; i < 8; ++i)
    {
        out[n] = e->key_id[i];
        ++n;
    }
    for (i = 0; i < 32; ++i)
    {
        out[n] = e->public_key[i];
        ++n;
    }
    n += sb_put_u64le(&out[n], e->not_before_ms);
    return n;
}

void sb_leaf_hash(const RegistryEntry *e, uint8_t out[SB_HASH_BYTES])
{
    uint8_t buf[1U + SB_ENTRY_BYTES];
    buf[0] = 0x00U;
    (void)sb_encode_entry(e, &buf[1]);
    sb_sha512(buf, 1U + SB_ENTRY_BYTES, out);
}

void sb_node_hash(const uint8_t left[SB_HASH_BYTES],
                  const uint8_t right[SB_HASH_BYTES],
                  uint8_t out[SB_HASH_BYTES])
{
    uint8_t buf[1U + (2U * SB_HASH_BYTES)];
    uint8_t tmp[SB_HASH_BYTES];
    uint32_t i;
    buf[0] = 0x01U;
    for (i = 0U; i < SB_HASH_BYTES; ++i)
    {
        buf[1U + i] = left[i];
        buf[1U + SB_HASH_BYTES + i] = right[i];
    }
    sb_sha512(buf, sizeof(buf), tmp);
    for (i = 0U; i < SB_HASH_BYTES; ++i)
    {
        out[i] = tmp[i];
    }
}

void sb_merkle_root(uint8_t (*nodes)[SB_HASH_BYTES], uint32_t n,
                    uint8_t out[SB_HASH_BYTES])
{
    uint32_t j;
    if (n == 0U)
    {
        sb_sha512(NULL, 0U, out);
        return;
    }
    while (n > 1U)
    {
        uint32_t w = 0U;
        uint32_t i;
        for (i = 0U; (i + 1U) < n; i += 2U)
        {
            sb_node_hash(nodes[i], nodes[i + 1U], nodes[w]);
            ++w;
        }
        if ((n & 1U) != 0U)
        {
            for (j = 0U; j < SB_HASH_BYTES; ++j)
            {
                nodes[w][j] = nodes[n - 1U][j];
            }
            ++w;
        }
        n = w;
    }
    for (j = 0U; j < SB_HASH_BYTES; ++j)
    {
        out[j] = nodes[0][j];
    }
}

/* "STARBOARD:REG/1" || log_size(8) || root(64) || revocation_root(64) ||
   epoch(8) || key_id(8) = 167 bytes. This is what the registry key signs. */
uint32_t sb_encode_snapshot(const RegistrySnapshot *s,
                            uint8_t out[SB_SNAPSHOT_BYTES])
{
    uint32_t n = sb_put_domain(out, SB_DOMAIN_SNAPSHOT);
    uint32_t i;
    n += sb_put_u64le(&out[n], s->log_size);
    for (i = 0U; i < SB_HASH_BYTES; ++i)
    {
        out[n] = s->root[i];
        ++n;
    }
    for (i = 0U; i < SB_HASH_BYTES; ++i)
    {
        out[n] = s->revocation_root[i];
        ++n;
    }
    n += sb_put_u64le(&out[n], s->epoch_ms);
    for (i = 0U; i < 8U; ++i)
    {
        out[n] = s->registry_key_id[i];
        ++n;
    }
    return n;
}

/* One Merkle layer in place with odd-node promotion; returns the new n. */
static uint32_t sb_merkle_fold_layer(uint8_t (*nodes)[SB_HASH_BYTES],
                                     uint32_t n)
{
    uint32_t w = 0U;
    uint32_t i;
    uint32_t j;
    for (i = 0U; (i + 1U) < n; i += 2U)
    {
        sb_node_hash(nodes[i], nodes[i + 1U], nodes[w]);
        ++w;
    }
    if ((n & 1U) != 0U)
    {
        for (j = 0U; j < SB_HASH_BYTES; ++j)
        {
            nodes[w][j] = nodes[n - 1U][j];
        }
        ++w;
    }
    return w;
}

/* Inclusion proof for leaf `index` over n leaf hashes (nodes[] destroyed).
   Ground-side tooling; craft only verify. */
bool sb_merkle_proof(uint8_t (*nodes)[SB_HASH_BYTES], uint32_t n,
                     uint64_t index, InclusionProof *out)
{
    uint32_t j;
    if ((n == 0U) || (index >= (uint64_t)n))
    {
        return false;
    }
    out->leaf_index = index;
    out->depth = 0U;
    while (n > 1U)
    {
        const uint64_t sib = index ^ 1ULL;
        if (sib < (uint64_t)n)
        {
            if (out->depth >= SB_PROOF_MAX_DEPTH)
            {
                return false;
            }
            for (j = 0U; j < SB_HASH_BYTES; ++j)
            {
                out->siblings[out->depth][j] = nodes[(uint32_t)sib][j];
            }
            ++out->depth;
        }
        n = sb_merkle_fold_layer(nodes, n);
        index >>= 1U;
    }
    return true;
}

/* Verify an inclusion proof against a root (flight path: bounded work).
   The layer count is advanced as ceil(n / 2) = (n >> 1) + (n & 1), which
   cannot overflow (the C++ reference's (n + 1) >> 1 wraps to 0 for
   log_size == UINT64_MAX; for every other log_size the two agree). */
bool sb_verify_inclusion(const uint8_t leaf[SB_HASH_BYTES],
                         const InclusionProof *p, uint64_t log_size,
                         const uint8_t root[SB_HASH_BYTES])
{
    uint8_t cur[SB_HASH_BYTES];
    uint64_t idx;
    uint64_t n;
    uint32_t used = 0U;
    uint32_t j;

    if ((log_size == 0ULL) || (p->leaf_index >= log_size) ||
        (p->depth > SB_PROOF_MAX_DEPTH))
    {
        return false;
    }
    for (j = 0U; j < SB_HASH_BYTES; ++j)
    {
        cur[j] = leaf[j];
    }
    idx = p->leaf_index;
    n = log_size;
    while (n > 1ULL)
    {
        const uint64_t sib = idx ^ 1ULL;
        if (sib < n)
        {
            if (used >= p->depth)
            {
                return false;
            }
            if ((idx & 1ULL) != 0ULL)
            {
                sb_node_hash(p->siblings[used], cur, cur);
            }
            else
            {
                sb_node_hash(cur, p->siblings[used], cur);
            }
            ++used;
        }
        idx >>= 1U;
        n = (n >> 1U) + (n & 1ULL);
    }
    return (used == p->depth) && sb_bytes_equal(cur, root, SB_HASH_BYTES);
}

/* SHA-512("STARBOARD:REV/1" || count(8) || key_id(8) * count), in the exact
   shipped order. */
void sb_revocation_hash(const uint8_t (*revoked_key_ids)[8], uint32_t count,
                        uint8_t out[SB_HASH_BYTES])
{
    Sha512State st;
    uint8_t hdr[SB_REVOCATION_HDR_BYTES];
    uint32_t n;
    uint32_t i;
    sb_sha512_init(&st);
    n = sb_put_domain(hdr, SB_DOMAIN_REVOCATION);
    n += sb_put_u64le(&hdr[n], (uint64_t)count);
    sb_sha512_update(&st, hdr, n);
    for (i = 0U; i < count; ++i)
    {
        sb_sha512_update(&st, revoked_key_ids[i], 8U);
    }
    sb_sha512_final(&st, out);
}

/* The pipeline (SPEC §8.2, REGISTRY.md §6) — normative order; every failed
   check returns TRUST_ASSERTED (conservative, never fatal). */
TrustLevel sb_establish_trust(const CapabilityAssertion *assertion,
                              const RegistryEntry *entry,
                              const InclusionProof *proof,
                              const RegistrySnapshot *snapshot,
                              const uint8_t (*revoked_key_ids)[8],
                              uint32_t revoked_count,
                              const uint8_t registry_public_key[32],
                              uint64_t now_ms,
                              uint64_t max_snapshot_age_ms)
{
    uint8_t snap_payload[SB_SNAPSHOT_BYTES];
    uint8_t rev_h[SB_HASH_BYTES];
    uint8_t lh[SB_HASH_BYTES];
    uint8_t ap[SB_ASSERTION_BYTES];
    uint32_t sn;
    uint32_t an;
    uint32_t i;

    /* 1. The snapshot is authentic (signed by the on-board registry key). */
    sn = sb_encode_snapshot(snapshot, snap_payload);
    if (!sb_ed25519_verify(snapshot->signature, snap_payload, (uint64_t)sn,
                           registry_public_key))
    {
        return TRUST_ASSERTED;
    }
    /* 1a. The snapshot is fresh: not future-dated, not older than the bound. */
    if (now_ms < snapshot->epoch_ms)
    {
        return TRUST_ASSERTED;
    }
    if ((now_ms - snapshot->epoch_ms) > max_snapshot_age_ms)
    {
        return TRUST_ASSERTED;
    }
    /* 2. The shipped revocation list is the pinned one; key not on it. */
    sb_revocation_hash(revoked_key_ids, revoked_count, rev_h);
    if (!sb_bytes_equal(rev_h, snapshot->revocation_root, SB_HASH_BYTES))
    {
        return TRUST_ASSERTED;
    }
    for (i = 0U; i < revoked_count; ++i)
    {
        if (sb_bytes_equal(revoked_key_ids[i], assertion->key_id, 8U))
        {
            return TRUST_ASSERTED;
        }
    }
    /* 3. The entry binds THIS craft to THIS key, is in force, and is in the
          log the snapshot signed. */
    if (!sb_id_equal(&entry->object_id, &assertion->object_id))
    {
        return TRUST_ASSERTED;
    }
    if (!sb_bytes_equal(entry->key_id, assertion->key_id, 8U))
    {
        return TRUST_ASSERTED;
    }
    if (now_ms < entry->not_before_ms)
    {
        return TRUST_ASSERTED;
    }
    sb_leaf_hash(entry, lh);
    if (!sb_verify_inclusion(lh, proof, snapshot->log_size, snapshot->root))
    {
        return TRUST_ASSERTED;
    }
    /* 4. The assertion is signed by the bound key and inside its window. */
    an = sb_encode_assertion(assertion, ap);
    if (!sb_ed25519_verify(assertion->signature, ap, (uint64_t)an,
                           entry->public_key))
    {
        return TRUST_ASSERTED;
    }
    if ((now_ms < assertion->epoch_ms) || (now_ms > assertion->expiry_ms))
    {
        return TRUST_ASSERTED;
    }
    return TRUST_VERIFIED;
}
