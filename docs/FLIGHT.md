# Starboard on a Spacecraft — Flight Integration Guide

**Flight layer v0.6.0 · ruleset SB/1 (`RULESET_ID 0x53420001`) · C99, MISRA-checked**

This is the document a flight-software team integrates from. It covers what
runs on board, what it needs (memory, stack, CPU, inputs), what it emits
(telemetry, maneuver requests, flock notices), how it fails safe, and how an
operator brings it into service. Every number here is measured, and the
script that measures it is named next to it.

> **Status, honestly.** Everything below runs and is verified in software:
> on 11 CPU architectures under emulation, bare-metal on Cortex-M
> microcontrollers, with fault injection, fuzzing and model-checked proofs.
> It has **not** flown, has not run on physical flight hardware, and has
> not been through an independent verification and validation campaign.
> It is ready for a hardware-in-the-loop test campaign and a demonstration
> mission in shadow mode. It is not yet a certified flight article.
> See [`OPEN-PROBLEMS.md`](OPEN-PROBLEMS.md).

---

## 1. What runs on board

```
          uplink / ISL                    on board (ports/c, one static context)
 ┌──────────────────────┐     ┌──────────────────────────────────────────────────┐
 │ CCSDS CDM (KVN text) │────▶│ CDM binding  ──▶ canonical shared input           │
 │ registry outcome     │────▶│ trust table      (exact decimal, frames, TCA)     │
 │ flock notices (ISL)  │──┐  │        │                                          │
 └──────────────────────┘  │  │        ▼                                          │
                           │  │ decide ×2 ──compare──▶ (mismatch: ×3, vote)       │
                           │  │   screen · Pc · rules · give-way burn · return    │
                           │  │        │                                          │
                           │  │        ▼                                          │
                           │  │ encounter table (16) ──▶ CCSDS telemetry          │──▶ HK / EVENT
                           │  │        │                 maneuver request         │──▶ GNC (AUTONOMOUS)
                           │  │        ▼                                          │
                           └─▶│ flock ledger (64) ◀──▶ maneuver notices           │◀─▶ ISL / relay
                              │ built-in self-test at boot and on command         │
                              └──────────────────────────────────────────────────┘
```

| Module | File | What it does |
|---|---|---|
| CDM binding | `cdm.c` | CCSDS 508.0-B-1 KVN → lattice states, inertial covariance, GPS-ms TCA. Exact decimal ingest; no `strtod`. |
| Verdict path | `canon.c` `geometry.c` `propagator.c` `covariance.c` `screening.c` `rules.c` | The protocol itself (SPEC §3–§5): J2 search, Starboard-Pc, the five rules. |
| Maneuver | `maneuver.c` | Min-Δv Clohessy-Wiltshire give-way burn, mirror+phase and quarter-period returns. |
| Multi-body | `multibody.c`, `broadphase.c` | Window cascade (SPEC §7) and constellation-scale candidate pruning. |
| Trust | `sha512.c` `ed25519.c` `registry.c` | Verify-only offline trust chain (REGISTRY.md), portable 32-bit arithmetic. |
| Flight layer | `fsw.c`, `ccsds.c` | Modes, self-test, redundant evaluation, encounter table, telemetry. |
| The flock | `flock.c` | Layer 4: maneuver notices, gossip ledger, flock check (§7). |
| Soft double | `softdouble.c` | Correctly-rounded double add/sub for FPU-less ARM (§5.3). |

API headers: `starboard_fsw.h` (flight layer), `starboard_cdm.h`,
`starboard_flock.h`, `starboard_registry.h`, `starboard.h` (protocol core).

## 2. Resource budget (measured)

`tools/baremetal.sh` — Cortex-M7, `arm-none-eabi-gcc -O2 --gc-sections`, the
whole public API linked, no `printf`:

| Resource | Value | Notes |
|---|---|---|
| Code + read-only data | **43.2 KB** | entire flight library |
| Initialized data | 84 B | |
| Static RAM (`.bss`) | **24.8 KB** | one `SbFsw` context (16 encounters, 32 counterparts) + one `SbFlock` ledger (64 notices) |
| Worst-case stack | **6.4 KB** | static call-graph bound, `tools/stack_depth.py`; deepest chain is a redundant re-decision through the J2 integrator |
| Heap | **0** | no `malloc` anywhere |
| Recursion | **none** | proved: the call graph is acyclic (`stack_depth.py` fails on a cycle) |
| OS services | **none** | no clock reads, no I/O; time is an input |

Capacities are compile-time constants (`SB_FSW_MAX_ENCOUNTERS`,
`SB_FSW_MAX_COUNTERPARTS`, `SB_FLOCK_MAX_ENTRIES`); scale them to the
mission and re-run the script.

**Work per call** is bounded by those constants and the pinned screening
arc: one decision is one bounded J2 search (≤ 1024 s at 4 s RK4 steps, a
48-step bisection) plus a 32-node Pc contour, done twice for redundancy. On
an x86-64 ground workstation a decision takes well under a millisecond. On
target, measure with your own cycle counter: QEMU is not cycle-accurate, so
this guide quotes no on-target timing.

## 3. Operating modes

| Mode | Decides | Telemeters | Commands burns | Entered when |
|---|---|---|---|---|
| `BOOT` | – | – | – | power-on, before the first self-test |
| `SAFE` | no | HK only | **never** | self-test failed, or commanded |
| `ADVISORY` | yes | HK + events | **never** | self-test passed, mission config says so — *shadow mode* |
| `AUTONOMOUS` | yes | HK + events | yes, for give-way decisions | self-test passed + mission config / ground command |

Leaving `SAFE` re-runs the full self-test; it fails closed. Demoting
`AUTONOMOUS → ADVISORY` cancels pending maneuver requests at the next poll.

**Recommended concept of operations.** Fly the first months in `ADVISORY`.
Starboard decides every CDM the operator uplinks, and the ground compares
those decisions with its own conjunction process. Promote to `AUTONOMOUS`
only after that comparison is clean. `sbctl cdm` on the ground computes
the *exact* decision the spacecraft computes (same library, same bits).

## 4. Inputs

### 4.1 Conjunction data messages

`sb_fsw_ingest_cdm(ctx, kvn, len, now_gps_ms, &slot)` takes the CCSDS CDM text
exactly as a screening provider issues it. The binding (SPEC §3.3a):

* identities from `INTERNATIONAL_DESIGNATOR` (COSPAR, the SPEC §3.2 id), or
  `CAT<catalog number>` when the CDM says `UNKNOWN`;
* states and RTN covariance **at TCA**, converted to SI with exact decimal
  ingest. A number with more than 15 significant digits, or one outside
  ±10²², is rejected rather than approximated;
* `REF_FRAME` GCRF / ICRF / EME2000 / TEME pass through; ITRF(any) is mapped
  by the frozen-Earth rotation at TCA;
* RTN → inertial covariance with the pinned expansion order, then the lattice;
* TCA → GPS milliseconds with integer calendar arithmetic and the pinned
  leap-second table.

**Fail-safe:** a CDM that cannot be bound is *rejected and reported*
(`SB_FSW_ERR_CDM` plus a `SbCdmStatus` in HK). It is never treated as
"no conjunction". So are a CDM that does not name this spacecraft, and one
whose TCA has passed.

**Rolling CDMs.** A CDM for the same counterpart with a TCA within ±2
conjunction-id buckets (±32.8 s) of a tracked encounter replaces it. That is
how provider updates converge.

### 4.2 Registry outcomes

`sb_fsw_set_counterpart(ctx, id, trust, tier, can_maneuver, now)` records the
result of `sb_establish_trust` (or of a ground-side verification) for a
counterpart and re-decides every open encounter with it. With no record, the
counterpart is `UNKNOWN`: SPEC Table 8-1 treats it as unable to maneuver, as
debris.

### 4.3 Configuration

`SbFswConfig`: own id, tier, capability, operating mode, APID base, preferred
burn lead (default suggestion: ~½ orbit, `2 700 000` ms), command latency.
The burn is placed at `TCA − lead`, or at `now + latency` if that is later.
In the second case the decision is flagged `late`.

## 5. Fault detection, isolation and recovery

### 5.1 Built-in self-test

Runs at `sb_fsw_init` and on `SAFE → *` transitions. `sbctl selftest` prints
it. Each bit, when set, means the test FAILED:

| Bit | Test | Catches |
|---|---|---|
| 0 `ARITH_FMA` | `a·b+c` with a contraction-sensitive triple | a build with FMA contraction on |
| 1 `ARITH_EXCESS` | double-rounding + excess-range probes | x87-style extended precision |
| 2 `TRANSCEND` | `canon_exp`/`canon_sincos` anchors **+ the full 12 290-value sweep fold** | any arithmetic corner that skews the pinned kernels |
| 3 `SEARCH` | J2 search on the SB-013 pair: TCA, miss, Pc bits | propagator / Pc / search defects |
| 4 `VERDICT` | rule chain + conjunction id from both perspectives | rule-logic or hashing defects |
| 5 `MANEUVER` | CW burn reaches κ·R_SAFE | maneuver-planner defects |
| 6 `SHA512` | FIPS 180-2 "abc" | hash defects |
| 7 `ED25519` | RFC 8032 test 1 verify + a 1-bit forgery reject | signature-verifier defects |
| 8 `CDM` | binding known answer (fold of a bound ITRF CDM) | parser / frame / epoch defects |
| 9 `CONSTANTS` | CRC-32 of every canonical constant's bit pattern | corrupted `.rodata`, wrong image |
| 10 `ARITH_ROUND` | six IEEE-754 rounding probes at exponent gaps 31–34 | broken software floating point (§5.3) |

Any failure latches `SAFE`. These are known-answer tests: they compare
against pinned bits, so they cannot pass on a platform that would disagree
with its peers.

### 5.2 Single-event upsets

* **Redundant evaluation.** Every decision is computed twice and compared
  field by field. Because the computation is bit-deterministic, *any*
  difference is a hardware fault, so no tolerance threshold is needed.
  On a mismatch a third run votes 2-of-3 (`seu_corrected`). With no
  majority the decision is withheld and `SB_FSW_ERR_FAULT` is reported
  (`seu_uncorrected`); nothing is commanded.
* **Stored-input integrity.** Each accepted CDM is CRC-32'd over a canonical
  serialization at ingest and re-checked before every re-decision. A
  mismatch drops the encounter and reports it (`input_corruptions`); a
  fresh CDM is required.
* **Comparisons never touch raw struct memory.** Padding bytes are not
  preserved by struct copies, and a raw `memcmp` raised false SEU alarms
  during development. Both the comparison and the CRC use canonical
  serializations.

Verified by fault injection: `tests/test_fsw.c` built with
`-DSB_FSW_FAULT_INJECTION` flips bits in the datapath. A single upset is
corrected; upsets in all three runs are contained, with nothing commanded.

### 5.3 Non-conformant floating point

Starboard's guarantee is that two spacecraft compute the *same bits*.
A platform whose double arithmetic is not IEEE-754 correctly rounded breaks
that silently. The cross-architecture campaign found one such platform:
**ARM soft-float double addition**, libgcc's `__aeabi_dadd`. It misrounds a
subtraction whose exponent gap is exactly 33 when the result renormalizes,
and Starboard's cosine kernel hits that case. Every FPU-less ARM MCU uses
this path, including Cortex-M0/M3, and Cortex-M4F, whose FPU is
single-precision only. Two layers of defence:

1. the self-test detects it (bits 2 and 10) and refuses to fly;
2. `softdouble.c` replaces the routine with a correctly-rounded one,
   verified against x86-64 hardware on 49 million cases. With it linked,
   Cortex-M3 and M4F produce the same bits as every other platform.

Details: [`evidence/CROSS-ARCH.md`](evidence/CROSS-ARCH.md).

### 5.4 Input validation (SPEC §3.7)

Rejected, never silently accepted: non-finite numbers, |r| outside
[6 000, 50 000] km, |v| outside [1, 20] km/s, negative or absurd variances,
unknown frames, duplicate fields, impossible dates, NUL bytes in identities
(found by differential fuzzing). In the core, a NaN screening arc can no
longer hang the search, and an out-of-domain propagation step can no longer
hit undefined behaviour. A non-finite covariance yields Pc = 1 (fail safe)
instead of a NaN that would disable the probability gate.

## 6. Telemetry

CCSDS Space Packet (133.0-B-2) telemetry. Primary header: version 0,
type TM, secondary-header flag set, unsegmented. Secondary header: 8-byte
big-endian GPS milliseconds. Trailer: CRC-16/CCITT-FALSE over the whole
packet. All payload fields are big-endian; doubles are IEEE-754 binary64.
`sb_fsw_poll_tm` returns one packet per call: HK first, then events, then
maneuver requests.

**HK** (`apid_base`, 60 B): fsw version u32 · ruleset u32 · mode u8 ·
self-test mask u32 · CDMs accepted u32 · rejected u32 · evaluations u32 ·
SEU corrected u32 · SEU uncorrected u32 · input corruptions u32 ·
maneuver requests u32 · last error u8 · last CDM error u8 · active
encounters u8.

**EVENT** (`apid_base+1`, 115 B), one per new or changed decision:
conjunction id u64 · counterpart id 12 B · TCA u64 · miss f64 · Pc f64 ·
found · actionable · role (0 none / 1 give-way / 2 stand-on) · rule (1–5) ·
advisory · late · burn-required (u8 each) · burn epoch u64 · Δv RTN 3×f64 ·
|Δv| f64 · return Δv f64.

**MANEUVER REQUEST** (`apid_base+2`, 152 B), `AUTONOMOUS` mode only:
conjunction id u64 · burn epoch u64 · Δv ECI 3×f64 · Δv RTN 3×f64 · |Δv| f64
· mirror-burn epoch u64 · mirror Δv RTN 3×f64 · final-burn epoch u64 ·
final Δv RTN 3×f64. Starboard *requests*; the spacecraft's GNC executes,
applying its own thruster and attitude constraints.

## 7. The flock (Layer 4) — every craft knows what the others are doing

When a craft commits to a give-way burn, the host calls
`sb_fsw_notice(ctx, slot, buf)`. That builds a 204-byte **maneuver notice**
(PROTOCOL.md §6): who moves, when, the Δv, the post-burn trajectory, and
both return burns. The host broadcasts it on inter-satellite links or a
shared relay. Each receiver:

1. folds it into its **flock ledger** with `sb_flock_receive`, de-duplicated
   by (origin, sequence), so newer plans replace older ones;
2. **relays** it once, with the hop count decremented, so the notice spreads
   through the constellation hop by hop. In `examples/murmuration.c` a
   notice reaches all 72 satellites of a 6×12 shell in 9 hops, and every
   ledger ends identical (`sb_flock_digest`);
3. **checks** the mover's announced path against its own with
   `sb_flock_check`, over the whole burn → coast → mirror → phase → return
   window. If the mover would pass within R_SAFE, it raises a FLOCK ALERT.

The flock **warns, it never decides.** A notice cannot make a craft burn:
an alert requests a fresh CDM and notifies the ground, and the verdict still
comes from shared data. The notice is also redundant by construction. The
give-way burn is a pure function of the shared CDM, so any craft holding
that CDM computes the mover's notice **bit for bit without receiving it**
(`tests/test_fsw.c`, "silent flock"). A forged notice costs at most a re-check.

**Dense formations.** In a tight cluster (the murmuration demo's Act II:
8 craft 2 km apart), a single give-way burn's drift and return sweep
through the neighbours' slots. The flock catches it: exactly the affected
neighbours raise alerts, and every member agrees on which. Because every
member holds the same CDM and ledger, the cluster can then screen candidate
schedules against the whole flock and the threat, and all members reach the
same choice. Two candidate families are a solo burn at several leads and the
whole formation turning *in unison*. The demo shows that neither family is
always safe and picks the cheapest conflict-free plan. Formation-follow
(members flying the leader's burn) is an *intra-operator* policy. Between
operators the flock only warns.

## 8. Integrating with a flight framework

The library has a C ABI with one context struct and no threads, OS or heap,
so it drops into any executive:

* **NASA cFS:** wrap it in an app whose command handler maps ground
  commands to `sb_fsw_set_mode` / `sb_fsw_set_counterpart` / CDM file loads
  (`sb_fsw_ingest_cdm`), and whose housekeeping wakeup calls
  `sb_fsw_poll_tm` and publishes the packets on the Software Bus. The
  packets are already CCSDS Space Packets. Note that "SB" is the cFS
  Software Bus, so name the app `STARBOARD` or similar.
* **F´ (F Prime):** an active component with a CDM file port, a mode
  command, and a telemetry-packet output port fed from `sb_fsw_poll_tm`.
* **Bare metal / RTOS:** call from one task; `sb_fsw_*` is not re-entrant
  on a single context. Run the self-test at boot and on a schedule.

Build: `make -C ports/c` produces `libstarboard.a`. Cross-compile with
`CC=<target>-gcc`. A conformant build MUST keep `-ffp-contract=off`, and on
FPU-less ARM MUST link `softdouble.c` (included in the library). Run
`tools/crossarch.sh` / `tools/baremetal.sh` for your target class, then the
self-test on the real hardware: it is the final arbiter.

## 9. Ground tool

```
sbctl cdm FILE [--self ID] [--verified] [--json]   # what each craft decides
sbctl selftest                                     # the flight self-test
```

Built from the same library as the spacecraft, so the ground sees exactly
the bits the spacecraft computes. `--json` is for integration into ground
conjunction-assessment pipelines.
