# Starboard — Open Problems

*An honest ledger of what SB/1 does **not** yet settle. A give-way convention
earns trust by being precise about its own edges; this file is where they are
named. Nothing here undermines the core guarantees (Theorems 1–4, the
bit-agreement result) — these are the boundaries of the current draft and the
work between "rigorous reference" and "flown standard."*

Status legend: **[spec]** needs a normative decision · **[impl]** needs code ·
**[eco]** needs an ecosystem partner or process · **[assurance]** would raise
confidence but isn't blocking correctness.

**v0.6 progress at a glance:** #2 partly closed (the CDM binding), #3
portability closed + Wycheproof-validated, #5 flight-grade Stage 1 in C, #7
architectures + bare-metal covered in emulation, #8 Theorem 1 machine-checked,
#10 closed. #1 is mitigated for CDM input by TCA anchoring. New: #11 (ARM
soft-float), #12 (flock trust and formations).

---

## 1. Dynamics fidelity beyond J2 / the bounded arc  **[spec]** — *mitigated for CDM input*

*v0.6:* the CDM binding (DESIGN §3.3a) anchors the shared input at the
provider's TCA and only back-propagates 512 s; the canonical integrator
round-trips to < 1 mm (SB-014), so the agreed encounter geometry *is* the
provider's high-fidelity geometry. J2-only fidelity now matters only for
locating the canonical TCA near the provider's, and for inputs that are not
TCA-anchored. Still open: a quantified truth-model study (J2–J6, drag across
solar activity, third-body) of both regimes.

The canonical propagator is two-body + J2 zonal over a bounded 1024 s arc
(§4.2), chosen so the total dynamics-model error stays ~1 m — inside the
covariance. Drag, SRP, higher zonals (J3…), and lunisolar third-body are
deliberately **excluded** from the shared computation and folded into the
covariance / arc bound. Open:

- A normative treatment of **longer lead times**. Operationally we re-screen
  against a fresh CDM as TCA approaches (rolling CDMs), but the cadence that
  keeps the between-CDM error inside the covariance is stated, not proven
  end-to-end against a truth propagator + realistic refresh intervals.
- Whether specific regimes (very low LEO with high drag; high area-to-mass)
  warrant a richer canonical model or a tighter arc. SB/1 is **LEO-scoped**;
  MEO/GEO/HEO would need their own perturbation budget (lunisolar is
  non-negligible there).

## 2. CDM provenance in practice  **[eco]** — *binding implemented*

*v0.6:* the **mapping** half is done: CCSDS 508.0-B-1 KVN ingest with exact
decimal conversion, pinned frames and epochs, in two cross-checked
implementations and a ground tool (`sbctl`). Still open: the ecosystem half
below, the XML/JSON CDM encodings, and provider authentication of CDMs (an
Ed25519 provider signature would close the ghost-conjunction nuisance of
DESIGN §8.4 at its source).

§3.3 pins that both craft must evaluate from the *same* state+covariance at a
common epoch, from a shared source. The protocol is correct given that input;
producing it is an ecosystem dependency not solved in-repo: who issues the
CDM, mapping to/from the CCSDS CDM standard, provider cadence and SLAs, and
the trust in the provider itself. This is exactly where a tracking-data
partner (LeoLabs-class) becomes load-bearing (STRATEGY §6).

## 3. Hand-rolled Ed25519 is not independently audited  **[assurance]** — *portable + Wycheproof-validated*

*v0.6:* the C99 port verifies Ed25519 with ref10-style 32-bit limbs (no
`__int128`). Both implementations pass all 210 Project Wycheproof vectors and
agree on 27 000 differential cases. The audit itself is still open.

The reference verifier is verify-only, passes the RFC 8032 vectors, an
adversarial review, and small-order-key hardening — but hand-rolled
signature verification going into flight software is a real risk. For
production, a vetted library behind the pluggable verifier hook is
recommended. It also uses `unsigned __int128` (a GCC/Clang extension); the
MISRA-C port must supply a portable 64×64→128 path built from 32-bit limbs
for rad-hard MCUs without a 128-bit type.

## 4. Registry operationalization  **[eco]**

REGISTRY.md specifies the key ceremony, rotation, backup root, current-state
revocation, freshness bound, and a CT-style public log — but none of this is
exercised by a running service. A reference registry (append-only signed log
+ auditor tooling) is future work (STRATEGY Stream 3) and a prerequisite for
any flight use of signed capability assertions.

## 5. Broad-phase candidate count is Θ(N²) at fixed shell density  **[impl]**

*v0.6:* Stage 1 is now in the C99 flight library: allocation-free,
recursion-free (heapsort), with a total order so its output is deterministic.
The fixed-capacity Stage 2 remains open.

Stage 1 (radial-band) is recall-1.0 and O(N log N) to decide; Stage 2
(spatial-hash) cuts the candidate constant ~30× and decides in O(N·slices).
But the candidate *count* stays a constant fraction of N² because a dense
fixed-altitude shell physically contains Θ(N²) near pairs — not an algorithm
limit. Very-large-catalog operation therefore needs distributed screening or
acceptance of the physical pair count. Also: the flight-grade,
allocation-free, fixed-capacity Stage 2 (the demo uses host-side containers)
is an identified productization step, not yet in the shipped header.

## 6. Maneuver model is Clohessy-Wiltshire linear  **[spec]**

The give-way burn and mirror+phase return use CW linear relative dynamics —
exact enough for the small Δv and short horizons here, and off the bit-exact
verdict path (checked to tolerance, not bits). A nonlinear / perturbed
maneuver optimizer, and validation against a high-fidelity propagator with
finite-burn effects, are out of the current scope.

## 7. No flight or hardware-in-the-loop validation  **[assurance]** — *emulation done, silicon open*

*v0.6:* bit-identical v1 + v2 transcripts on 11 CPU architectures (incl.
RAD750-class PowerPC, RISC-V, SPARC, big-endian MIPS/s390x) and bare-metal
Cortex-M3/M4F/M7, all under QEMU (`docs/evidence/CROSS-ARCH.md`). A flight
self-test now runs the known answers on the unit itself at every boot.
Still open: physical hardware, HIL, cycle-accurate timing, a fixed-point
profile, a demonstration mission.

Every result here is software on a workstation. Still open: a build on an
actual rad-hard target (or a cycle-accurate model), the promised fixed-point
path (the core is `double` today), and a sim-flight or on-orbit demonstration.
Bit-agreement is proven across host compilers/optimization levels (and, once
the C port lands, across languages) — not yet across target architectures.

## 8. Machine-checked proofs  **[assurance]** — *Theorem 1 machine-checked*

*v0.6:* CBMC proves Theorem 1's decision layer for every possible predicate
function plus the conjunction-id lemma, and memory safety of the notice and
CDM parsers (`docs/evidence/PROOFS.md`). Still open: the real predicates over
fully symbolic doubles (did not finish), Theorems 2–4, the broad-phase
superset property, and a proof assistant development.

Theorems 1–4 have proofs short enough to verify by inspection, plus empirical
pins (the swap-invariance sweep, the recall check). A machine-checked proof —
at least of Theorem 1's totality and agreement, and the broad-phase superset
property — would raise assurance beyond by-hand review.

## 9. Trademark and name clearance  **[eco]**

`Starboard Certified™` is used throughout as an **unregistered** mark (the
`™` marking is legitimate without filing). The project launched publicly
ahead of registration by explicit decision (STRATEGY §9): a mark is worth
filing when there is adoption to defend. Revisit on the first external
adopter, certification inquiry, or confusing fork.

## 10. Cross-language port scope  **[impl]** — *closed in v0.6*

*v0.6:* the C99 port now covers Ed25519, the full registry pipeline, CW
maneuvers and both returns, the multi-body cascade and the broad-phase, all
cross-checked against C++ bit for bit by the v2 transcript. The fixed-point
profile remains future work.

The MISRA-C99 port (`ports/c/`) covers the **verdict path** plus SHA-512 /
registry hashing — the surface whose cross-implementation bit-agreement is the
product claim — and reproduces the C++ transcript byte-for-byte (29-build
matrix). It does **not** yet cover Ed25519 verification, the CW maneuver code,
or the broad-phase; those share the C++ reference's guarantees but are not (yet)
cross-language bit-checked. Extending the port to those, and to a fixed-point
profile, is the remaining implementation work.


## 11. Software floating point on FPU-less ARM  **[assurance]** — *new*

The cross-architecture campaign found that ARM soft-float double addition
(libgcc `__aeabi_dadd`, GCC 13) misrounds subtractions at an exponent gap of
exactly 33 (`docs/evidence/CROSS-ARCH.md`). Starboard detects it at boot and
ships a verified replacement (`ports/c/softdouble.c`). Open: confirmation on
silicon, an upstream report, and the same scrutiny for other soft-float
libraries (RISC-V without D, vendor toolchains): the self-test is the
defence, but each new platform class deserves its own campaign.

## 12. The flock: authentication and formation policy  **[spec]** — *new*

Layer 4 (DESIGN §7.7) warns and never decides, which bounds what a forged
notice can do. Open: authenticated notices without signing keys on board
(the registry design keeps craft verify-only), rate limiting under a notice
flood, and a *normative* formation-maneuver policy. The murmuration demo
shows candidate screening against the flock, but which candidates a
cross-operator formation may consider, and how ties are broken, is not yet
specified.

---

*If you are evaluating Starboard: items 2 (the ecosystem half) and 4 gate a
real deployment, with 7 (physical hardware) gating flight; the rest are
assurance and productization. The intellectual
core — deterministic, covariance-aware, provably-agreeing give-way from a
shared CDM — is settled and tested (see `docs/DESIGN.md`, `conformance/`).*
