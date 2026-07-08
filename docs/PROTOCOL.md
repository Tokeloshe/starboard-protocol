# Starboard — Wire Protocol (Layer 2 handshake)

*Draft v0.1. The handshake is **optional** — Layers 0 and 1 are correct with no
communication. This layer only confirms and optimizes when a link exists.*

## Design principles

- **Tiny.** Messages fit in a single CCSDS-style frame; usable over a
  constrained inter-satellite link or a shared ground provider's relay.
- **Idempotent.** Re-sending a message never changes the outcome.
- **Advisory, never authoritative.** If a message contradicts the deterministic
  Layer-1 verdict, the verdict wins and the discrepancy is logged.

## Messages

All messages share an 8-field header, then a typed body. Field encoding is
CBOR (compact, schema-free) in the reference; a fixed binary layout is defined
in Appendix A for flight software that cannot afford a CBOR codec.

```
Header {
  version      u8      // = 1
  msg_type     u8      // 1=INTENT, 2=ACK, 3=CLEAR
  conj_id      u64     // deterministic conjunction id = hash(min(idA,idB), max(idA,idB), TCA_epoch)
  sender_id    str     // canonical COSPAR/NORAD id
  receiver_id  str
  t_ca_epoch   u64     // TCA as GPS epoch ms (both derive the same value)
  sent_epoch   u64
  sig          bytes   // optional signature over the header+body (capability trust)
}
```

### INTENT  (give-way craft → stand-on craft)
```
INTENT {
  give_way_id   str     // who is maneuvering (must equal Layer-1 verdict)
  deciding_rule u8      // 1..5, for cross-check
  delta_v_mps   [f32;3] // planned burn in the craft body frame
  burn_epoch    u64
}
```
Stand-on craft cross-checks `give_way_id`/`deciding_rule` against its own
Layer-1 result. Match → `ACK`. Mismatch → `ACK` with `dispute=true` and both
fall back to the deterministic tuple; the event is flagged for the conformance
log.

### ACK  (stand-on craft → give-way craft)
```
ACK {
  agree     bool
  dispute   bool     // true if the stand-on craft computed a different verdict
}
```

### CLEAR  (give-way craft → stand-on craft)
```
CLEAR {
  achieved_miss_m f32   // realized miss distance after the maneuver
  returning       bool  // true once Layer-3 return-to-slot begins
}
```

## State machine (give-way craft)

```
 SCREEN ──conjunction──▶ DECIDE ──I give way──▶ SEND_INTENT ──ACK──▶ MANEUVER
   ▲                       │                                            │
   │                    I stand on                                      ▼
   └──────────────────── HOLD ◀──────────────── RETURN_TO_SLOT ◀──── SEND_CLEAR
```

The stand-on craft's machine is trivial: on `INTENT` it verifies, `ACK`s, and
holds its orbit until `CLEAR` (or TCA + margin) — predictability is its job.

## Appendix A — fixed binary layout (flight profile)
`TODO(v0.2)`: byte-exact little-endian layout for MCU targets without a CBOR
library. Fixed 24-byte header + fixed-width 10-char ids.

## Appendix B — conjunction id
`conj_id = FNV-1a-64( sorted(idA,idB) ‖ round(t_ca_epoch, 1s) )` so both craft
independently derive the same id without exchanging it first.
