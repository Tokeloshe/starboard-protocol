# Starboard Certified™ — Conformance and Certification

**Suite version 1 · for ruleset SB/1 (`RULESET_ID 0x53420001`)**

*This document defines what "Starboard compatible" means. The definition is
precise, testable, and owned: the conformance vectors are normative, the
suite is the test, and the `Starboard Certified™` mark is the only
permitted public claim of compatibility. The key words MUST/SHOULD/MAY are
RFC 2119.*

## 1. Why conformance is the product

The protocol's entire value is that any two compliant craft — built by
different vendors, flown by rival operators — compute the **same** answer.
That property cannot be eyeballed; it must be tested against pinned
vectors. So the suite is not QA for the reference implementation: it is the
**operational definition of the standard**, and certification against it is
what an insurer, regulator, or launch-licensing authority can point to.

## 2. Conformance levels

### SB-C1 — Verdict conformance (MUST for the mark)

For every vector SB-001…SB-012 the candidate implementation produces the
expected `detected`/`actionable` flags, `deciding_rule`, `give_way_id`,
`advisory` flag, and (for SB-007) the window assignment — evaluated from
**both** craft's perspectives, which must agree (Theorem 1).

### SB-C2 — Bit conformance (MUST for flight software)

Additionally, bit-equality on the pinned anchors: Starboard-Pc for SB-001
(`0x3F5117B3E92585E2`), the `canon_exp` anchors of SB-010, and every
conjunction id. Maneuver quantities (Δv vectors, return plans) are checked
to 10⁻⁹ relative tolerance — they are single-craft outputs, not
cross-craft agreements, so tolerance is sufficient there.

### SB-C3 — Closed-loop conformance (SHOULD)

A scenario-level replay: the candidate flies the demo timeline (screen →
verdict → burn → TCA → mirror+phase return) and the 3-body window in a
simulation harness, meeting `d ≥ R_SAFE` at TCA and returning to slot
within 1 m. This is the level a certification engagement actually runs.

## 3. The vectors

| Vector | Exercises | Expected |
|---|---|---|
| SB-001 | crossing geometry | Rule 4; lower-id craft gives way; **Pc bit anchor** |
| SB-002 | overtaking (speed + stern cone + closing) | Rule 3; chaser gives way |
| SB-003 | head-on, lateral offset (symmetric starboard) | Rule 5 coin |
| SB-004 | verified disabled craft | Rule 1; able craft gives way |
| SB-005 | verified CREWED vs OPERATIONAL | Rule 2 |
| SB-006 | exact tie (vertical head-on) | Rule 5 coin; total |
| SB-007 | 3-body window with derelict | capability verdict, then CASCADE lock |
| SB-008 | spoofed tier, unverified craft | trust downgrade; falls to physics; advisory when both unverified |
| SB-009 | same miss, tight vs fat covariance | Pc gate flips actionability |
| SB-010 | the pinned transcendental | `canon_exp` bit anchors + 10⁻¹³ accuracy |
| SB-011 | Theorem 2 | double give-way displacements oppose |
| SB-012 | 4096-geometry LCG sweep | swap-invariance + totality, empirically |

Inputs are defined in `conformance/runner.cpp` (the runner is normative:
its scenario constants ARE the vectors). Run: `cmake -B build && cmake
--build build && ctest --test-dir build`.

## 4. Certification process

1. Operator/vendor runs the suite against their stack (self-test, free —
   the suite is AGPL like the rest of the repo).
2. Certification engagement: the certifying authority (the mark owner or a
   licensed lab) reruns the suite on the candidate's build artifacts,
   including SB-C2 bit checks on the candidate's target compiler/CPU with
   the Strict Arithmetic Profile flags, plus SB-C3 replay.
3. Grant: per-constellation, annual, revocable. Revocation triggers include
   failing a re-test after a software update and **attributable protocol
   misbehaviour in flight** (a signed false assertion, or logged disputes
   traced to non-conformant computation — see DESIGN.md §8.4).
4. The mark: only certified stacks may use `Starboard Certified™` in
   marketing, insurance filings, or licensing submissions.

## 5. Versioning

- The **ruleset** (SB/1) changes only with a new `RULESET_ID`. Because the
  id is hashed into every conjunction id, two ruleset versions cannot be
  confused in flight — migration is explicit, fleet-wide, and coordinated.
- The **suite** may add vectors (suite version bump) without changing the
  ruleset; certified implementations MUST pass added vectors at their next
  annual recertification.
- Pinned bit anchors never change within a ruleset version. If an anchor is
  found to be wrong, that is a ruleset revision, not a patch.

## 6. The interoperability trap (why "almost compatible" is worthless)

A stack that deviates from ANY normative value — a constant, the rule
order, the Pc series, the hash — will still *run*, which is exactly the
danger. Against the installed base it will sometimes compute the opposite
verdict; the failure is silent until two craft each decide the other gives
way. The suite exists so that "compatible" is a fact you can test, not a
claim you can make. Consequently a tweaked fork faces a trilemma: pass the
suite (then it isn't a fork), fail the suite (then it cannot carry the
mark or interoperate), or fork the suite too (then it is a new, empty
network competing against the installed base from zero). See DESIGN.md §11
and STRATEGY.md §5.
