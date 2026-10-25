# Changelog

All notable changes to Starboard. The **specification** version (ruleset
identity) and the **repository** state are tracked together here; the
normative spec history also lives in `docs/DESIGN.md` Appendix B.

Versions before first public release share `RULESET_ID = 0x53420001`; the
id-bump discipline (a ruleset change forces a new `RULESET_ID`, DESIGN §11)
begins at publication.

---

## v0.6.0 — "flight edition"

From a rigorous reference to software a spacecraft team can integrate and
test. **No pinned bit of SB/1 changed**: the v1 transcript and every SB-C2
anchor are untouched (`RULESET_ID` unchanged). All additions are new layers,
new input paths, or bit-neutral hardening.

### Real-world input: the CDM binding (SPEC §3.3a)
- CCSDS 508.0-B-1 **Conjunction Data Message** ingest, in two independent
  implementations (`ports/c/cdm.c`, `core/include/starboard/cdm.hpp`)
  cross-checked bit for bit.
- **Exact decimal ingest**: ≤ 15 significant digits and |e| ≤ 22 make one
  IEEE operation correctly rounded; no `strtod`. Fuzzed against glibc over
  119 M inputs.
- Pinned frames (ITRF via the frozen-Earth rotation at TCA), GPS epochs with
  a pinned leap table, RTN → inertial covariance in pinned order.
- **TCA anchoring**: back-propagate 512 s and search forward, so the
  agreed encounter geometry is the provider's high-fidelity TCA geometry.
- `sbctl`: decide any CDM from the ground with the flight library itself
  (`--json` for pipelines). Example CDMs in `examples/cdm/`.

### The flight layer (`docs/FLIGHT.md`)
- One static context, C API, no heap or OS: modes SAFE / ADVISORY (shadow) /
  AUTONOMOUS; a bounded encounter table with rolling-CDM updates.
- **Built-in self-test**: eleven known-answer tests at boot and on command;
  any failure latches SAFE.
- **SEU tolerance**: every decision computed twice and compared field by
  field (exact, because the computation is deterministic), 2-of-3 vote on
  mismatch, CRC-protected stored inputs. Verified by fault injection.
- **CCSDS Space Packet** telemetry: housekeeping, decision events, maneuver
  requests, with a CRC-16 trailer.
- Fits a CubeSat MCU: 43.2 KB code, 24.8 KB RAM, 6.4 KB worst-case stack
  (static bound), recursion-free (proved).

### Layer 4: the flock (SPEC §7.7, PROTOCOL §6)
- 204-byte **maneuver notices**, hop-by-hop gossip with de-duplication, a
  bounded **flock ledger** with an order-independent digest, and the
  **flock check** of each mover's path against one's own. It warns and never
  decides; notices are predictable from shared data ("silent flock").
- `examples/murmuration.c`: a 72-satellite shell converges in 9 hops; an
  8-craft formation detects that a lone give-way burn would sweep through
  its neighbours, and screens candidate plans, including turning in unison.

### C99 port: now the complete flight library
- Ported maneuver (CW, give-way, both returns), multi-body cascade,
  broad-phase (non-recursive heapsort), Ed25519 verification with portable
  32-bit limbs (no `__int128`), and the full registry trust pipeline.
- A second, informative **v2 transcript** pins all of it cross-language.

### Evidence (`docs/ASSURANCE.md`)
- **11 CPU architectures** bit-identical under QEMU (x86-32/64, AArch64,
  ARMv7, ARMv5 soft-float, RAD750-class PowerPC, PPC64LE, RISC-V, SPARC64,
  MIPS, s390x); **bare-metal Cortex-M3/M4F/M7** bit-identical.
- Non-conformance controls (x87, FMA) are refused by the self-test.
- **Finding:** ARM soft-float double addition (libgcc `__aeabi_dadd`)
  misrounds at an exponent gap of exactly 33, which would desynchronize
  FPU-less ARM satellites. Detected by the self-test; fixed by
  `ports/c/softdouble.c` (49 M cases verified against hardware).
- **CBMC proofs**: Theorem 1 decision layer (all predicate functions),
  conjunction-id lemma, memory safety of notice and CDM parsing.
- **Fuzzing** (libFuzzer + ASan/UBSan, ~129 M executions), including
  C-vs-C++ differential fuzzing that found an identity-truncation bug.
- **MISRA C:2012** record with documented deviations (`tools/misra.sh`).
- **CI** (`.github/workflows/ci.yml`) re-runs all of it on every push.

### Fixes (bit-neutral for valid input)
- A NaN screening arc no longer loops forever (a watchdog reset in flight).
- `propagate` guards non-finite and > 2²⁰ s steps (was UB / hours-long loop).
- Non-finite covariance → Pc := 1 instead of a NaN that disabled the gate.
- Embedded NUL in a CDM identity can no longer truncate it to a valid id.

### Discoverability
- `CITATION.cff`, `codemeta.json`, `llms.txt`; README rewritten for a
  general audience.

## v0.5.1 (unreleased draft) — "pre-publication audit"

A full-code review before the first public release; every finding fixed,
mirrored into the C99 port, and re-verified. **No pinned bit changed**: the
golden `--bits` transcript and all SB-C2 anchors are untouched.

### Verdict-path semantics
- **Linear kernel out-of-window branch** (`geometry.hpp`, DESIGN §4.2a): a
  TCA in the past *or beyond the horizon* is now assessed at the current
  separation — the earlier draft silently dropped an approaching pair
  already inside the screening bubble whose TCA lay past the horizon.
- **Orbital-search inside-bubble fallback** (`screening.hpp`): the matching
  fix for the bounded-arc search, plus a first-sample clamp for arcs
  shorter than one grid step, and an incremental grid march (bit-identical,
  ~16× cheaper).
- **PSD repair** (`covariance.hpp`, DESIGN §4.4): a malformed or hostile
  covariance with `c² > a·b` no longer drives Pc to NaN — which compared
  false against `PC_ACTIONABLE` and silently *disabled* the actionability
  gate. Canonical repair: drop the correlation.

### Maneuver & multi-body
- **Pc-only actionability plans a burn** (`maneuver.hpp`, DESIGN §6.4):
  previously a conjunction actionable on Pc alone (miss already at target)
  planned nothing and the risk was never mitigated.
- **Theorem 2 restored in counter-rotating head-on** (`maneuver.hpp`): the
  degenerate zero-miss push direction is now the pinned perpendicular of
  the *shared* encounter axis, signed by canonical pair order — an
  own-frame cross-track direction failed to oppose when the two Hill frames
  are mirrored.
- **Advisory verdicts record no locks** (`multibody.hpp`, DESIGN §7.3): an
  unverifiable derelict could acquire a give-way lock and "absorb" a real
  conjunction via the cascade. Malformed windows (out-of-range or
  self-paired indices) are rejected before anything is indexed.

### API & hygiene
- `evaluate()` dropped its unused `Conjunction` parameter: the verdict
  consumes descriptors + epoch only (the conjunction gates *whether* it
  runs).
- Runtime FP-contraction self-check (`arithmetic_profile_ok()`, DESIGN
  §3.5); the conformance runner and C99 transcript binary refuse to start
  if it fails.
- Shared test scaffolding deduplicated into `conformance/support.hpp`;
  SB-007 rebuilt on physically-screened three-body geometry; SB-014 now
  also covers the two-impulse CW return; `bitcheck.sh --pin` refuses to pin
  from a build that fails its own conformance vectors.

## v0.5 (unreleased draft) — "canonical reference"

The arc from an impressive first draft to a field-defining reference:
deterministic *and* physically valid, validated against independent
references, and proven bit-identical across compilers **and languages**.

### Layer 0 — screening
- **Canonical J2 propagator** (`propagator.hpp`): two-body + J2 zonal,
  fixed-step RK4, over a bounded 1024 s screening arc. Replaces the earlier
  two-body-only ephemeris, which diverged from real LEO motion by tens of km
  within an orbit — the craft would have "agreed" on a fiction. Measured
  total dynamics-model error over the arc: ~1 m, inside the covariance.
- **Conjunction data message (CDM) input model** (DESIGN §3.3): the shared
  input is both craft's state+covariance at a common reference epoch from a
  shared source. Makes Theorem 1's symmetric-input premise *established*, not
  assumed.
- **Starboard-Pc via Patera's contour integral** (`covariance.hpp`): replaces
  Chan's equivalent-area series, which erred 15–43% on the elongated/
  correlated covariance that is the LEO norm (a circularization *method*
  error, not truncation). Patera's error is set by node count, not covariance
  shape: ~1e-12 vs an independent adaptive quadrature at any aspect ratio.
- **Constellation-scale broad-phase** (`broadphase.hpp`, DESIGN §4.8):
  radial-band sweep with recall = 1.0 proven *unconditionally* (it relaxes
  screen_orbits' own gate), plus a demonstrated temporal spatial-hash Stage 2
  with per-object provable speed bounds.

### Determinism & conformance
- **Independent MISRA-C99 port** (`ports/c/`) reproducing the reference
  `--bits` transcript byte-for-byte: 29 builds (C++ and C, gcc/clang,
  −O0…−O3) collapse to a single hash. Cross-*language* agreement is the
  strongest evidence the spec, not an implementation accident, defines the
  bits.
- **No-FP-contraction pinned in-source**: `#pragma STDC FP_CONTRACT OFF`
  (honored by Clang/C11, verified to hold under `-mfma`) + the build flag for
  GCC. Closes a real hole where an FMA build shifted verdict-path bits.
- **Independent Pc validator** (`conformance/validation/`): re-derives the
  projection from the spec and integrates with scipy adaptive quadrature; the
  gate that drove the Chan→Patera change.
- Conformance vectors **SB-001…SB-020** (added SB-013/014 orbital search &
  propagator, SB-015…018 crypto/registry, SB-019 snapshot freshness, SB-020
  broad-phase recall). `tools/bitcheck.sh` runs the cross-compiler +
  cross-language matrix.

### Security / trust
- **Registry** (`registry.hpp`, `docs/REGISTRY.md`): offline trust chain —
  signed capability assertion → operator key → Merkle inclusion proof →
  signed snapshot → on-board root key, with current-state revocation.
- **Verify-only crypto** (`crypto/`): dependency-free SHA-512 and Ed25519
  verification; no signing key ever flies. Hardened to reject small-order
  public keys (repudiation defense).
- **Snapshot freshness bound**: closes a revocation-replay bypass (a stale
  but validly-signed snapshot could un-revoke a key).

### Docs
- `docs/DESIGN.md` rewritten as a normative RFC-style spec with four theorems
  (agreement, double-give-way safety, maneuver reversibility, multi-body
  convergence) proved by inspection.
- `docs/OPEN-PROBLEMS.md` (new): honest ledger of the ten remaining edges,
  tagged by whether each gates a real deployment.
- `docs/CONFORMANCE.md`, `docs/REGISTRY.md`, `docs/PROTOCOL.md`,
  `docs/STRATEGY.md` updated for the above.

### Known limitations
See `docs/OPEN-PROBLEMS.md`. Headline items gating deployment: perturbation
fidelity beyond J2 over longer horizons, CDM data-provider integration, and
registry operationalization. Assurance items: hand-rolled crypto audit,
on-target/fixed-point validation, machine-checked proofs.

---

## v0.1 — initial draft
Strategy, design, wire spec, and a C++ reference core seeded from an
orbital-mechanics codebase: the 5-rule give-way ruleset, naive
miss-distance screening, and the two-rival-sats demo.
