# Starboard — Open Problems

*An honest ledger of what SB/1 does **not** yet settle. A give-way convention
earns trust by being precise about its own edges; this file is where they are
named. Nothing here undermines the core guarantees (Theorems 1–4, the
bit-agreement result) — these are the boundaries of the current draft and the
work between "rigorous reference" and "flown standard."*

Status legend: **[spec]** needs a normative decision · **[impl]** needs code ·
**[eco]** needs an ecosystem partner or process · **[assurance]** would raise
confidence but isn't blocking correctness.

---

## 1. Dynamics fidelity beyond J2 / the bounded arc  **[spec]**

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

## 2. CDM provenance in practice  **[eco]**

§3.3 pins that both craft must evaluate from the *same* state+covariance at a
common epoch, from a shared source. The protocol is correct given that input;
producing it is an ecosystem dependency not solved in-repo: who issues the
CDM, mapping to/from the CCSDS CDM standard, provider cadence and SLAs, and
the trust in the provider itself. This is exactly where a tracking-data
partner (LeoLabs-class) becomes load-bearing (STRATEGY §6).

## 3. Hand-rolled Ed25519 is not independently audited  **[assurance]**

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

## 7. No flight or hardware-in-the-loop validation  **[assurance]**

Every result here is software on a workstation. Still open: a build on an
actual rad-hard target (or a cycle-accurate model), the promised fixed-point
path (the core is `double` today), and a sim-flight or on-orbit demonstration.
Bit-agreement is proven across host compilers/optimization levels (and, once
the C port lands, across languages) — not yet across target architectures.

## 8. Machine-checked proofs  **[assurance]**

Theorems 1–4 have proofs short enough to verify by inspection, plus empirical
pins (the swap-invariance sweep, the recall check). A machine-checked proof —
at least of Theorem 1's totality and agreement, and the broad-phase superset
property — would raise assurance beyond by-hand review.

## 9. Trademark and name clearance  **[eco]**

`Starboard Certified™` is used throughout as the conformance mark, but the
wordmark is not yet filed and the name is not cleared (STRATEGY §8). Public
launch stakes the name.

## 10. Cross-language port scope  **[impl]**

The C99 port targets the **verdict path** plus SHA-512 / registry hashing —
the surface whose cross-implementation bit-agreement is the product claim. It
does **not** cover Ed25519 verification or the CW maneuver code; those share
the C++ reference's guarantees but are not (yet) cross-language bit-checked.

---

*If you are evaluating Starboard: items 1, 2, and 4 are the ones that gate a
real deployment; the rest are assurance and productization. The intellectual
core — deterministic, covariance-aware, provably-agreeing give-way from a
shared CDM — is settled and tested (see `docs/DESIGN.md`, `conformance/`).*
