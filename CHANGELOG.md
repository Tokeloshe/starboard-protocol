# Changelog

All notable changes to Starboard. The **specification** version (ruleset
identity) and the **repository** state are tracked together here; the
normative spec history also lives in `docs/DESIGN.md` Appendix B.

Versions before first public release share `RULESET_ID = 0x53420001`; the
id-bump discipline (a ruleset change forces a new `RULESET_ID`, DESIGN §11)
begins at publication.

---

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
