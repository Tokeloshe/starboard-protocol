# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — registry fixture generator.
# Copyright (C) 2026 James H. All rights reserved.
#
# Generates conformance/registry_fixtures.hpp: a registry log, signed
# snapshot, inclusion proofs and capability assertions, produced by an
# INDEPENDENT implementation of the canonical encodings (this file) with
# signatures from libsodium (PyNaCl). If the C++ pipeline verifies these,
# two implementations of REGISTRY.md agree end to end.
#
#   Usage: python3 conformance/gen_fixtures.py > conformance/registry_fixtures.hpp
#   (Regenerate only when the canonical encodings change; the output is
#   committed so certification runs do not need Python.)

import hashlib
import struct

from nacl.signing import SigningKey

T0 = 1401000000000  # shared demo/conformance epoch (GPS ms)
DAY = 86400000


def sha512(b: bytes) -> bytes:
    return hashlib.sha512(b).digest()


def u64(v: int) -> bytes:
    return struct.pack("<Q", v)


def obj_id(s: str) -> bytes:
    b = s.upper().encode()
    assert len(b) <= 12
    return b + b"\x00" * (12 - len(b))


# --- canonical encodings (must mirror core/include/starboard/registry.hpp) ---
def encode_assertion(oid, tier, can_man, epoch, expiry, key_id):
    return (b"STARBOARD:CAP/1" + obj_id(oid) + bytes([tier, 1 if can_man else 0])
            + u64(epoch) + u64(expiry) + key_id)


def encode_entry(oid, key_id, pub, not_before):
    return b"STARBOARD:ENT/1" + obj_id(oid) + key_id + pub + u64(not_before)


def encode_snapshot(log_size, root, rev_root, epoch, reg_key_id):
    return (b"STARBOARD:REG/1" + u64(log_size) + root + rev_root + u64(epoch)
            + reg_key_id)


def revocation_hash(key_ids):
    h = hashlib.sha512()
    h.update(b"STARBOARD:REV/1" + u64(len(key_ids)))
    for k in key_ids:
        h.update(k)
    return h.digest()


def leaf_hash(entry_bytes: bytes) -> bytes:
    return sha512(b"\x00" + entry_bytes)


def node_hash(left: bytes, right: bytes) -> bytes:
    return sha512(b"\x01" + left + right)


def merkle_root(leaves):
    nodes = list(leaves)
    if not nodes:
        return sha512(b"")
    while len(nodes) > 1:
        nxt = [node_hash(nodes[i], nodes[i + 1])
               for i in range(0, len(nodes) - 1, 2)]
        if len(nodes) % 2:
            nxt.append(nodes[-1])
        nodes = nxt
    return nodes[0]


def merkle_proof(leaves, index):
    nodes = list(leaves)
    sibs = []
    while len(nodes) > 1:
        sib = index ^ 1
        if sib < len(nodes):
            sibs.append(nodes[sib])
        nxt = [node_hash(nodes[i], nodes[i + 1])
               for i in range(0, len(nodes) - 1, 2)]
        if len(nodes) % 2:
            nxt.append(nodes[-1])
        nodes = nxt
        index >>= 1
    return sibs


def key_fingerprint(pub: bytes) -> bytes:
    # REGISTRY.md §4: key id = first 8 bytes of SHA-512(public key).
    return sha512(pub)[:8]


# --- deterministic keys -------------------------------------------------------
def key_from(label: str) -> SigningKey:
    return SigningKey(sha512(b"starboard-fixture:" + label.encode())[:32])


reg_key = key_from("registry-root")
op_acme = key_from("operator-acme")
op_orbx = key_from("operator-orbx")
op_dead = key_from("operator-dead")   # will be revoked
op_fill = key_from("operator-fill")

REG_PUB = bytes(reg_key.verify_key)
REG_KID = key_fingerprint(REG_PUB)

# --- the log: five entries (odd count exercises the promote path) ------------
ENTRIES = [
    ("2026-001A", op_acme, T0 - 30 * DAY),
    ("2026-044B", op_orbx, T0 - 30 * DAY),
    ("1998-067X", op_dead, T0 - 300 * DAY),
    ("2025-500C", op_fill, T0 - 10 * DAY),
    ("2025-501D", op_fill, T0 - 10 * DAY),
]
entry_bytes = []
for oid, key, nb in ENTRIES:
    pub = bytes(key.verify_key)
    entry_bytes.append(encode_entry(oid, key_fingerprint(pub), pub, nb))
leaves = [leaf_hash(e) for e in entry_bytes]
root = merkle_root(leaves)

REVOKED = [key_fingerprint(bytes(op_dead.verify_key))]
rev_root = revocation_hash(REVOKED)

snap_payload = encode_snapshot(len(ENTRIES), root, rev_root, T0 - DAY, REG_KID)
snap_sig = reg_key.sign(snap_payload).signature

# --- assertions ---------------------------------------------------------------
def make_assertion(oid, tier, can_man, epoch, expiry, key):
    kid = key_fingerprint(bytes(key.verify_key))
    payload = encode_assertion(oid, tier, can_man, epoch, expiry, kid)
    return (oid, tier, can_man, epoch, expiry, kid,
            key.sign(payload).signature)


A_OK = make_assertion("2026-001A", 1, True, T0 - DAY, T0 + DAY, op_acme)
A_REVOKED = make_assertion("1998-067X", 1, False, T0 - DAY, T0 + DAY, op_dead)
A_EXPIRED = make_assertion("2026-044B", 1, True, T0 - 3 * DAY, T0 - DAY,
                           op_orbx)

# --- emit the header ------------------------------------------------------------
def hexs(b: bytes) -> str:
    return ",".join(f"0x{x:02X}" for x in b)


def arr(b: bytes, per=12, indent="    "):
    xs = [f"0x{x:02X}" for x in b]
    lines = [",".join(xs[i:i + per]) for i in range(0, len(xs), per)]
    return (",\n" + indent).join(lines)


print("// SPDX-License-Identifier: AGPL-3.0-or-later")
print("// Starboard Protocol — GENERATED registry fixtures. Do not edit.")
print("// Copyright (C) 2026 James H. All rights reserved.")
print("// Regenerate: python3 conformance/gen_fixtures.py (see that file).")
print("#pragma once")
print('#include "starboard/registry.hpp"')
print()
print("namespace fixtures {")
print(f"inline const uint8_t REG_PUB[32] = {{ {hexs(REG_PUB)} }};")
print(f"inline const uint64_t NOW_MS = {T0}ull;")
print()
print("inline const starboard::registry::RegistryEntry ENTRIES[5] = {")
for (oid, key, nb) in ENTRIES:
    pub = bytes(key.verify_key)
    kid = key_fingerprint(pub)
    print(f'    {{ starboard::make_object_id("{oid}"), {{ {hexs(kid)} }},')
    print(f"      {{ {hexs(pub)} }}, {nb}ull }},")
print("};")
print()
print(f"inline const uint8_t REVOKED_IDS[1][8] = {{ {{ {hexs(REVOKED[0])} }} }};")
print()
print("inline const starboard::registry::RegistrySnapshot SNAPSHOT = {")
print(f"    {len(ENTRIES)}ull,")
print(f"    {{ {arr(root)} }},")
print(f"    {{ {arr(rev_root)} }},")
print(f"    {T0 - DAY}ull,")
print(f"    {{ {hexs(REG_KID)} }},")
print(f"    {{ {arr(snap_sig)} }},")
print("};")
print()
print("// Inclusion proofs for every leaf (siblings zero-padded past depth).")
print("inline const starboard::registry::InclusionProof PROOFS[5] = {")
for i in range(len(ENTRIES)):
    sibs = merkle_proof(leaves, i)
    print(f"    {{ {i}ull, {len(sibs)}u, {{")
    for s in sibs:
        print(f"        {{ {arr(s, indent='          ')} }},")
    print("    } },")
print("};")
print()


def emit_assertion(name, a):
    (oid, tier, can_man, epoch, expiry, kid, sig) = a
    print(f"inline const starboard::CapabilityAssertion {name} = {{")
    print(f'    starboard::make_object_id("{oid}"),')
    print(f"    static_cast<starboard::Tier>({tier}), "
          f"{'true' if can_man else 'false'},")
    print(f"    {epoch}ull, {expiry}ull,")
    print(f"    {{ {hexs(kid)} }},")
    print(f"    {{ {arr(sig)} }},")
    print("};")


emit_assertion("ASSERT_OK", A_OK)
emit_assertion("ASSERT_REVOKED", A_REVOKED)
emit_assertion("ASSERT_EXPIRED", A_EXPIRED)
print("} // namespace fixtures")
