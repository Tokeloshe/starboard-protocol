# Starboard — Wire Protocol (Layer 2 handshake)

**Protocol v0.3 (draft) · for ruleset SB/1 (`RULESET_ID 0x53420001`)**

*The handshake is **optional** — Layers 0/1 are correct with no
communication (DESIGN.md Theorem 1). This layer confirms, cross-checks, and
optimizes when a link exists. The key words MUST/SHOULD/MAY are RFC 2119.*

## 1. Design principles

- **Tiny.** Every message fits one CCSDS-class frame; usable over a
  constrained inter-satellite link or a shared ground relay.
- **Idempotent.** Re-delivery, reordering, or duplication of any message
  never changes an outcome.
- **Advisory, never authoritative.** A message that contradicts the
  deterministic Layer-1 verdict is a logged **dispute**, not an override.
  If Layer 2 and Layer 1 ever disagree, Layer 1 wins and both craft flag
  the event for the conformance log (this is how implementation drift in
  the wild gets caught).

## 2. Header

All messages carry the same header, then a typed body. The reference
encoding is the fixed binary layout of Appendix A (little-endian, no
padding); CBOR MAY be used on links that already carry CBOR, but the fixed
layout is the conformance-tested profile.

```
Header {
  version      u8        // = 2
  msg_type     u8        // 1=INTENT, 2=ACK, 3=CLEAR
  ruleset_id   u32       // MUST equal 0x53420001 for SB/1
  conj_id      u64       // Appendix B — both craft derive it independently
  sender_id    char[12]  // canonical id (DESIGN.md §3.2)
  receiver_id  char[12]
  tca_epoch_ms u64       // shared TCA epoch (bit-identical on both craft)
  sent_epoch_ms u64
}
```

A receiver MUST discard (and SHOULD log) any message whose `ruleset_id`
differs from its own: different rules means the parties do not even agree
on what the encounter is called, let alone who gives way.

## 3. Messages

### 3.1 INTENT — give-way craft → stand-on craft

```
INTENT {
  give_way_id   char[12] // MUST equal the sender's Layer-1 verdict
  deciding_rule u8       // 1..6 (DESIGN.md §5.3) for cross-check
  advisory      u8       // 1 if the verdict was advisory (nobody trusted)
  dv_mps        f32[3]   // planned burn, sender's Hill frame (§3.1 frames)
  burn_epoch_ms u64
}
```

The stand-on craft cross-checks `give_way_id` and `deciding_rule` against
its own Layer-1 result. Match → `ACK{agree=1}`. Mismatch → `ACK{agree=1,
dispute=1}`: **both craft still act on their own deterministic verdicts**
(Theorem 2 makes the both-maneuver worst case safe), and the dispute is
flagged for the conformance log — a disagreement between two certified
stacks is, by Theorem 1, evidence that one of them is out of conformance.

### 3.2 ACK — stand-on craft → give-way craft

```
ACK { agree u8; dispute u8; }
```

### 3.3 CLEAR — give-way craft → stand-on craft

```
CLEAR {
  achieved_miss_m f32   // realized separation at TCA
  returning       u8    // 1 once the mirror+phase return has begun
}
```

## 4. State machines

```
 give-way craft:
 SCREEN ──actionable──▶ DECIDE ──I give way──▶ [SEND INTENT] ──▶ BURN
    ▲                     │                                        │
    │                 I stand on                              TCA passes
    │                     ▼                                        ▼
    └───────────────── HOLD ◀── in slot ── RETURN(mirror+phase) ◀─ [SEND CLEAR]

 stand-on craft:
 SCREEN ──actionable──▶ DECIDE ──I stand on──▶ HOLD (predictably)
                                                │  on INTENT: verify, ACK
                                                ▼  on CLEAR or TCA+margin:
                                              resume normal ops
```

Timeouts: a give-way craft that receives no ACK proceeds anyway (the link
was optional). A stand-on craft that receives no CLEAR by `TCA + 2·
TCA_BUCKET` treats the window as closed.

## 5. Security

Messages SHOULD be signed (Ed25519 over `"STARBOARD:MSG/1" ‖ header ‖
body`, 64 bytes appended) with the sender's registry-bound operator key —
the same key, fingerprint rule, and revocation regime that govern
capability assertions ([`REGISTRY.md`](REGISTRY.md); the domain prefix
keeps message signatures unreplayable as assertions and vice versa).
Receivers verify with the same offline chain (`registry.hpp`). Unsigned
traffic is processed but its `dispute`/`advisory` claims are treated at
ASSERTED trust. A forged INTENT cannot redirect anyone: the receiver acts
on its own verdict regardless (§3.1), so the worst a forger achieves is a
logged dispute under someone else's identity — which the signature
requirement then removes.

## 6. Layer 4 — the flock: MANEUVER NOTICE

Layer 2 is a conversation between the two craft of one encounter. Layer 4
is a **broadcast to everyone**: when a craft commits to a give-way burn it
announces the burn, its post-burn trajectory and its return, so that every
craft in the constellation shares one picture of who is moving
(DESIGN.md §7.7). Notices are relayed hop by hop; the flock **warns, it
never decides**.

### 6.1 Gossip rules

* A receiver decodes the frame (CRC-32, format, ruleset, finite and physical
  values, causal epoch order) and rejects anything else silently (counted).
* It ignores its own notices echoed back.
* Identity of a notice is (origin, conjunction id); a higher `seq` replaces a
  lower one, and an equal or lower `seq` is a duplicate. **Duplicates are
  never relayed.** That alone bounds the traffic: each craft relays each
  notice at most once.
* A NEW or UPDATED notice with `hops > 0` is relayed with `hops − 1`. The
  originator sets `hops = 16` by default, enough for the diameter of a
  realistic inter-satellite-link mesh.
* The ledger is bounded (64 entries in the reference). A full ledger
  rejects new origins until entries expire, which happens once their return
  has completed (plus a retention margin).
* Ledger agreement is checkable: `sb_flock_digest` is an order-independent
  digest of the ledger content, excluding hop counts.

### 6.2 Wire format (204 bytes, little-endian, packed)

| Offset | Field | Size |
|---|---|---|
| 0 | magic `"SBFN"` | 4 |
| 4 | version (= 1) | 1 |
| 5 | flags (reserved, 0) | 1 |
| 6 | hops remaining | 1 |
| 7 | reserved (0) | 1 |
| 8 | ruleset_id | 4 |
| 12 | origin id (the maneuvering craft) | 12 |
| 24 | seq (origin's notice counter) | 4 |
| 28 | conjunction id | 8 |
| 36 | counterpart id (what it avoids) | 12 |
| 48 | tca_epoch_ms (GPS) | 8 |
| 56 | burn_epoch_ms (GPS) | 8 |
| 64 | post-burn position, ECI (m, f64×3) | 24 |
| 88 | post-burn velocity, ECI (m/s, f64×3) | 24 |
| 112 | burn Δv, ECI (m/s, f64×3) | 24 |
| 136 | mirror-burn epoch (GPS ms) | 8 |
| 144 | mirror-burn Δv, origin's Hill frame (f64×3) | 24 |
| 168 | final-burn epoch (GPS ms) | 8 |
| 176 | final-burn Δv, origin's Hill frame (f64×3) | 24 |
| 200 | CRC-32 (IEEE 802.3) over bytes 0–199 **with byte 6 zeroed** | 4 |

The hop byte is excluded from the CRC because relays decrement it. The CRC
protects against corruption, not forgery. A notice MAY be authenticated
like any Layer-2 message (§5). An unauthenticated notice is processed at
ASSERTED trust, which costs nothing because notices only ever warn.

### 6.3 Why a forged notice is harmless

A notice can cause re-checks, alerts and requests for fresh CDMs. It cannot
cause a burn, change a verdict or suppress an alert, because verdicts come
from the shared CDM (DESIGN.md §3.3). And for every craft holding that CDM,
the honest notice is *predictable*: the give-way plan is a pure function of
shared data, so a receiver can recompute the mover's notice and compare it
bit for bit.

## Appendix A — fixed binary layout (flight profile)

Little-endian, packed, no alignment padding. Sizes in bytes.

| Offset | Field | Size |
|---|---|---|
| 0  | version | 1 |
| 1  | msg_type | 1 |
| 2  | ruleset_id | 4 |
| 6  | conj_id | 8 |
| 14 | sender_id | 12 |
| 26 | receiver_id | 12 |
| 38 | tca_epoch_ms | 8 |
| 46 | sent_epoch_ms | 8 |
| 54 | body (below) | — |

Bodies: INTENT = give_way_id(12) ‖ deciding_rule(1) ‖ advisory(1) ‖
dv_mps(3×f32 LE) ‖ burn_epoch_ms(8) = 34 B → 88 B total.
ACK = agree(1) ‖ dispute(1) = 2 B → 56 B. CLEAR = achieved_miss_m(f32) ‖
returning(1) = 5 B → 59 B. Optional signature: +64 B trailer. Every message
fits comfortably in a 128-byte frame.

## Appendix B — conjunction id (normative)

```
conj_id = FNV-1a-64( lo_id[12] ‖ hi_id[12] ‖
                     LE64(tca_epoch_ms / 16384) ‖ LE32(RULESET_ID) )
```

with offset basis 14695981039346656037 and prime 1099511628211, `lo`/`hi`
by canonical id order. Both craft derive the same id without exchanging it:
`tca_epoch_ms` is computed from lattice-quantized shared inputs and is
therefore bit-identical on both sides (DESIGN.md §3.4–§3.5); the 2¹⁴ ms
bucket additionally keeps the id stable across small catalog refreshes. An
id remains valid while the recomputed TCA stays within ±2 buckets
(hysteresis); beyond that it is a new conjunction.
