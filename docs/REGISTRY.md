# The Starboard Registry — identity, keys, and revocation

**Registry specification v0.1 (draft) · for ruleset SB/1**

*The key words MUST/SHOULD/MAY are RFC 2119. Reference implementation:
`core/include/starboard/registry.hpp` (verification),
`core/include/starboard/crypto/` (SHA-512, Ed25519 verify).*

## 1. What the registry is for

Rules 1–2 of the ruleset consume *asserted* facts — "I cannot maneuver",
"I am CREWED". DESIGN.md §8 makes safety independent of those assertions
(every unverified claim degrades toward physics), but *fuel fairness and
right-of-way* still reward honesty only if honesty is checkable. The
registry is how a craft checks, **fully offline, at decision time**:

```
capability assertion ──Ed25519──▶ operator key
operator key binding ──Merkle inclusion proof──▶ log root
log root + revocation list ──Ed25519──▶ registry root key (on board)
```

Three signatures and one hash chain; no ground contact, no clock beyond the
shared epoch, no state beyond a snapshot the craft carries.

## 2. Actors and keys

| Actor | Key | Held where |
|---|---|---|
| **Registry** (the certification authority of CONFORMANCE.md) | Ed25519 root key | HSM on the ground; only signs snapshots |
| **Operator** | Ed25519 operator key(s) | operator ground segment; signs capability assertions |
| **Craft** | none required | carries the registry root *public* key + a current snapshot |

Craft never hold signing keys: the flight side is verify-only, which is why
the reference crypto module implements no signing path at all (no nonce
generation, no secret handling — the classic Ed25519 implementation
hazards are structurally absent).

**Key fingerprint rule (§4):** an operator key's `key_id` is the first 8
bytes of SHA-512 of the 32-byte public key. Assertions name keys by
fingerprint; the registry entry carries the full key.

## 3. The log, snapshots, and proofs

The registry log is **append-only**: each entry binds `(object_id, key_id,
public_key, not_before)`. Entries are hashed into an RFC 6962-compatible
Merkle tree over SHA-512 (leaf prefix `0x00`, node prefix `0x01`,
odd-node promotion).

A **snapshot** is the registry state a craft carries:
`(log_size, log root, revocation_root, epoch, registry_key_id)` signed by
the registry root key. Operators distribute, alongside the snapshot, the
inclusion proofs for their own entries — a craft *asserting* is
responsible for supplying (via catalog metadata or the Layer-2 channel)
everything a counterpart needs to verify it. Verification cost on board:
one hash chain of ⌈log₂ n⌉ nodes and two Ed25519 verifications — bounded,
allocation-free (`PROOF_MAX_DEPTH = 64`).

Snapshots SHOULD be refreshed on the same cadence as catalog data. Two
craft MAY hold different snapshot epochs; a verdict never depends on the
counterpart's snapshot (each side verifies independently, and divergence is
bounded by Theorem 2 exactly like every other trust divergence).

**Freshness is mandatory (not optional).** A snapshot's signature attests
only that the registry *once* published that state — a validly-signed but
stale snapshot carries a stale *revocation list*, so accepting one lets an
adversary replay a pre-revocation snapshot to un-revoke a key. Therefore a
verifier MUST reject a snapshot that is dated in the future (`now <
epoch`, i.e. clock skew or rollback) or older than a freshness bound
`SNAPSHOT_MAX_AGE_MS` (default 30 days; operators SHOULD set it to a small
multiple of their refresh cadence). Beyond the bound the counterpart
degrades to ASSERTED — conservative, never trusted. Craft that can retain
state SHOULD additionally track the highest snapshot epoch they have
accepted and refuse older ones (monotonic rollback protection); the age
bound is the stateless floor that every craft enforces.

## 4. Revocation

Revocation is **current-state, not log-history**: the full list of revoked
`key_id`s ships with every snapshot, and its hash
(`revocation_root = SHA-512("STARBOARD:REV/1" ‖ count ‖ ids…)`) sits
*inside the signed snapshot payload*. Consequences:

* A revoked operator cannot resurrect an old binding by presenting an old
  entry + proof — the verifier checks the *current* list first.
* Nobody can strip the list in transit — the snapshot signature dies.
* The list stays small (revocations are rare events, 8 bytes each), so
  carrying it whole is cheaper and simpler than negative proofs.

Revocation triggers are certification-governance events (CONFORMANCE.md
§4): a signed-but-false assertion, key compromise, or decertification.

## 5. Canonical encodings (normative)

All little-endian, packed, no padding. Every structure carries a distinct
15-byte ASCII domain-separation prefix, so a signature over one structure
can never be replayed as another:

| Structure | Prefix | Layout after prefix | Total |
|---|---|---|---|
| capability assertion | `STARBOARD:CAP/1` | id(12) ‖ tier(1) ‖ can_maneuver(1) ‖ epoch_ms(8) ‖ expiry_ms(8) ‖ key_id(8) | 53 B |
| registry entry | `STARBOARD:ENT/1` | id(12) ‖ key_id(8) ‖ public_key(32) ‖ not_before_ms(8) | 75 B |
| snapshot payload | `STARBOARD:REG/1` | log_size(8) ‖ root(64) ‖ revocation_root(64) ‖ epoch_ms(8) ‖ registry_key_id(8) | 167 B |
| revocation list | `STARBOARD:REV/1` | count(8) ‖ key_id(8)×count | — |

The `revocation_root` hash is order-sensitive; the registry MUST ship (and
sign over) the list in ascending `key_id` order so independent auditors
reproduce the root. A verifier need not re-sort — it hashes the shipped
list and compares to the signed root — but a non-canonical order is a
registry conformance defect. `key_id` is an 8-byte fingerprint used only
for lookup; a fingerprint collision is harmless because the assertion
signature is verified under the *full* public key from the log entry, not
under the fingerprint.

Assertions carry an explicit validity window `[epoch_ms, expiry_ms]` and
MUST be re-issued per mission phase; a craft MUST NOT honor an assertion
outside its window (stale "cannot maneuver" claims age out by themselves).

## 6. The verification pipeline

`registry::establish_trust()` — the exact order is normative, and **every
failure returns ASSERTED**, never an error: a broken proof chain simply
means the counterpart is handled as debris (SPEC §8.3). The pipeline:

1. snapshot signature valid under the on-board registry root key;
1a. snapshot **fresh**: `epoch ≤ now ≤ epoch + SNAPSHOT_MAX_AGE_MS`
   (rejects future-dated and stale snapshots — the check that makes
   revocation binding, above);
2. shipped revocation list matches `revocation_root`; asserting key not on it;
3. entry binds this `object_id` to this `key_id`, is in force
   (`not_before`), and its leaf is included in the signed root;
4. assertion signature valid under the entry's public key, and `now` inside
   the assertion's validity window.

Conformance vectors SB-015…SB-018 pin the whole chain, with fixtures
generated by an independent implementation (Python + libsodium) — passing
them demonstrates cross-implementation agreement on every encoding and
hash in this document.

## 7. Key ceremony and governance (operational, informative)

* The registry root key is generated in a documented ceremony (two-person
  control, HSM, published key fingerprint). Its public key ships with
  flight software at integration; rotating it is a fleet software event,
  so a **backup root** SHOULD be provisioned at ceremony time and
  pre-trusted on board.
* Operator keys enter the log at certification and are the mark-holder's
  registration record (STRATEGY.md Stream 2: the registry *is* the
  certified-operator directory — governance and revenue share a mechanism).
* The log SHOULD be publicly auditable (published append-only, CT-style):
  the trees are RFC 6962-compatible precisely so existing transparency-log
  tooling can watch it. Craft do not audit; auditors on the ground do.

## 8. Threat review (what this buys, given DESIGN.md §8)

| Attack | Outcome |
|---|---|
| Spoof "cannot maneuver" without a key | ASSERTED → treated as debris; attacker gains a berth, loses nothing else; no safety impact |
| Spoof with a *signed* assertion | attributable evidence against a registry identity → revocation (§4) |
| Replay an old assertion | validity window (§5) bounds it; expired → ASSERTED |
| Replay a revoked operator's old entry+proof (with a *current* snapshot) | current-state revocation list (§4) catches it before the proof is even checked |
| Replay a *pre-revocation snapshot* (stale revocation list) | snapshot freshness bound (§3, step 1a) rejects it once past `SNAPSHOT_MAX_AGE_MS` → ASSERTED; monotonic epoch tracking closes the window entirely for stateful craft |
| Strip or trim the revocation list | snapshot signature fails → everything ASSERTED (conservative) |
| Forge/alter a snapshot | Ed25519 under the root key; fails → ASSERTED |
| Cross-protocol signature replay | domain separation (§5) |
| Compromise the registry root | the one catastrophic key; mitigations: HSM + ceremony + backup root + the fact that even a forged VERIFIED only shifts fuel, never safety (Theorem 2 and §8.4 still hold) |

The last row is the deep property: even a full registry compromise cannot
weaponize the protocol — it can only misallocate right-of-way, which the
physics-level conservatism absorbs.
