# Starboard — Wire Protocol (Layer 2 handshake)

**Protocol v0.2 (draft) · for ruleset SB/1 (`RULESET_ID 0x53420001`)**

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
