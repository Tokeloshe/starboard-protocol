// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — trust and the adversarial model (SPEC §8).
//
// Rules 1 and 2 consume ASSERTED facts (capability, tier) that a hostile or
// buggy craft could misstate. The defense is structural, not cryptographic
// cleverness:
//
//   Every unverified assertion degrades toward physics.
//
// A craft whose capability assertion cannot be verified is treated as unable
// to maneuver (you avoid it — exactly as you would debris), and its tier is
// clamped to OPERATIONAL. Consequences, proven in SPEC §8.3:
//   * Lying can reallocate WHO burns fuel; it can never remove the
//     requirement that someone avoids. Safety is lie-invariant.
//   * When both craft are downgraded, Rules 1-2 tie out and the decision
//     falls through to Rules 3-5 — pure geometry and the fair coin — which
//     CANNOT be spoofed without also changing the physical encounter.
//   * A signed-but-false assertion is attributable evidence against its
//     signer (registry key), which is what certification revocation is for.
//
// The reference implementation deliberately ships no cryptography: the
// signature scheme is pinned by the SPEC (Ed25519 over the canonical
// assertion bytes) and verification is a caller-supplied hook, so the core
// stays dependency-free and flight-portable.
// ============================================================================

#include "id.hpp"

#include <cstdint>

namespace starboard {

// Static, registry-backed criticality tier (SPEC §5.4, Rule 2).
// Higher value = higher priority = stands on.
enum class Tier : uint8_t {
    DEORBITING  = 0,
    OPERATIONAL = 1,
    CRITICAL    = 2,   // critical infrastructure / high-value cluster
    CREWED      = 3
};

// How well the local craft could verify the counterpart's assertion.
enum class TrustLevel : uint8_t {
    UNKNOWN  = 0,   // no assertion available (uncatalogued / silent object)
    ASSERTED = 1,   // assertion received but signature absent or unverifiable
    VERIFIED = 2    // signature checked against the registry key
};

// A signed capability assertion (SPEC §8.2, REGISTRY.md §5). The signature
// covers the domain-separated canonical encoding (registry.hpp,
// encode_assertion) under the operator key named by key_id.
struct CapabilityAssertion {
    ObjectId object_id;
    Tier     tier;
    bool     can_maneuver;
    uint64_t epoch_ms;        // validity window start (GPS ms)
    uint64_t expiry_ms;       // validity window end — assertions expire
    uint8_t  key_id[8];       // operator key fingerprint (registry binding)
    uint8_t  signature[64];   // Ed25519 over the canonical bytes
};

// Verification hook: platform supplies the crypto (SPEC pins Ed25519).
using SignatureVerifier = bool (*)(const CapabilityAssertion&);

// Conservative-default projection (SPEC §8.3, Table 8-1).
inline bool effective_can_maneuver(TrustLevel trust, bool asserted) {
    return (trust == TrustLevel::VERIFIED) ? asserted : false;
}

inline Tier effective_tier(TrustLevel trust, Tier asserted) {
    return (trust == TrustLevel::VERIFIED) ? asserted : Tier::OPERATIONAL;
}

} // namespace starboard
