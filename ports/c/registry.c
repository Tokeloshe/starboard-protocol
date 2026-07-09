// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// registry.c — RFC 6962 Merkle tree over SHA-512 (leaf/node hash + iterative
// in-place root). Only the pieces the --bits registry fold exercises.

#include "starboard.h"

static const char SB_DOMAIN_ENTRY[16] = "STARBOARD:ENT/1";

static uint32_t sb_put_u64le(uint8_t *out, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i)
    {
        out[i] = (uint8_t)(v >> (8 * i));
    }
    return 8u;
}

static uint32_t sb_put_domain(uint8_t *out, const char *domain)
{
    uint32_t i;
    for (i = 0u; i < 15u; ++i)
    {
        out[i] = (uint8_t)domain[i];
    }
    return 15u;
}

/* "STARBOARD:ENT/1" || id(12) || key_id(8) || pubkey(32) || not_before(8) */
static uint32_t sb_encode_entry(const RegistryEntry *e,
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
    uint8_t buf[1u + SB_ENTRY_BYTES];
    buf[0] = 0x00u;
    (void)sb_encode_entry(e, &buf[1]);
    sb_sha512(buf, 1u + SB_ENTRY_BYTES, out);
}

void sb_node_hash(const uint8_t left[SB_HASH_BYTES],
                  const uint8_t right[SB_HASH_BYTES],
                  uint8_t out[SB_HASH_BYTES])
{
    uint8_t buf[1u + (2u * SB_HASH_BYTES)];
    uint8_t tmp[SB_HASH_BYTES];
    uint32_t i;
    buf[0] = 0x01u;
    for (i = 0u; i < SB_HASH_BYTES; ++i)
    {
        buf[1u + i] = left[i];
        buf[1u + SB_HASH_BYTES + i] = right[i];
    }
    sb_sha512(buf, sizeof(buf), tmp);
    for (i = 0u; i < SB_HASH_BYTES; ++i)
    {
        out[i] = tmp[i];
    }
}

void sb_merkle_root(uint8_t (*nodes)[SB_HASH_BYTES], uint32_t n,
                    uint8_t out[SB_HASH_BYTES])
{
    uint32_t j;
    if (n == 0u)
    {
        sb_sha512(NULL, 0u, out);
        return;
    }
    while (n > 1u)
    {
        uint32_t w = 0u;
        uint32_t i;
        for (i = 0u; (i + 1u) < n; i += 2u)
        {
            sb_node_hash(nodes[i], nodes[i + 1u], nodes[w]);
            ++w;
        }
        if ((n & 1u) != 0u)
        {
            for (j = 0u; j < SB_HASH_BYTES; ++j)
            {
                nodes[w][j] = nodes[n - 1u][j];
            }
            ++w;
        }
        n = w;
    }
    for (j = 0u; j < SB_HASH_BYTES; ++j)
    {
        out[j] = nodes[0][j];
    }
}
