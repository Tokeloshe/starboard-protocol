# Starboard

**The Rules of the Orbital Road.**

An open, deterministic protocol that lets any two satellites — rival
operators, no radio contact, no ground station, no human in the loop —
independently compute the **same** answer for *who gives way* before a close
approach, execute a minimum-Δv avoidance, and flock back to their assigned
slots.

At sea this was solved 150 years ago: when two vessels cross, the one that
has the other to **starboard** gives way. Both skippers compute the same
answer from the same geometry — no negotiation. That single convention
(COLREGS) is why millions of ships share the water without a traffic
controller. Low Earth orbit — ~10,000 active satellites today, credible
plans for 100,000+ — still resolves cross-operator conjunctions by *email*.
Starboard is the missing convention.

## What makes it more than a good idea

The protocol is built so its central promise is **provable, testable, and
unforkable**:

- **Theorem 1 (agreement).** Every pairwise computation runs on the
  id-ordered pair — the *Canonical Pair Transform* — so
  `evaluate(A,B) = evaluate(B,A)` by construction, in every geometry,
  including degenerate ones. The rule chain is total: five rules, each
  decides or falls through, ending in a fair coin that is deterministic yet
  50/50 over repeated encounters (nobody is systematically the fuel-payer).
- **Determinism survives uncertainty.** Conjunctions are *found* by a
  canonical ephemeris (fixed-iteration Kepler — provably convergent, no
  data-dependent branching) and assessed as covariance-aware probability
  of collision (encounter-plane Pc via Chan's series at fixed order),
  with a power-of-two quantization lattice, a strict IEEE-754 arithmetic
  profile, and spec-pinned transcendentals — so two independent
  implementations produce **bit-identical** conjunctions, Pc, and
  verdicts. Not aspiration: `tools/bitcheck.sh` shows 20/20 compiler ×
  optimization builds byte-identical across a ~50,000-quantity transcript.
- **Theorem 2 (lies are safe).** Unverified claims degrade toward physics:
  an unverifiable craft is treated as debris and avoided; spoofing can
  reallocate fuel costs, never safety — even if *both* craft maneuver,
  their burns oppose by construction and separation grows. Honesty is
  checkable fully offline: signed capability assertions chain through a
  Merkle registry log to an on-board root key (`docs/REGISTRY.md`), with
  verify-only flight crypto — no signing key ever flies.
- **Theorem 3 (reversible maneuvers).** The give-way burn is an analytic
  minimum-norm impulse in Clohessy-Wiltshire dynamics, undone *exactly* by
  a mirror+phase return — total campaign cost ≈ Δv·(2 + 2/N), known in
  advance.
- **Theorem 4 (multi-body).** 3+ craft windows resolve in a single
  deterministic pass with role locks — every participant derives the same
  assignment without a byte exchanged.
- **Forks self-eject.** The ruleset version is hashed into every
  conjunction id: a stack that tweaks any rule no longer deconflicts — or
  even names encounters — consistently with the installed base. The
  conformance suite gives "compatible" a precise, owned, testable
  definition. See `docs/DESIGN.md` §11.

## See it run

```
cmake -B build && cmake --build build
./build/starboard_crossing_demo     # two rivals agree with zero comms,
                                    # then a 3-body window with a derelict
ctest --test-dir build              # the conformance suite (SB-001..019)
```

The demo prints the full timeline: independent screening with identical Pc
bits, independent identical verdicts, the min-Δv Hill-frame burn (with the
naive-impulse comparison), safe TCA passage, and the exact return to slot.

## Repository layout

```
starboard-protocol/
├── docs/
│   ├── DESIGN.md          The specification: rules, theorems, constants (normative)
│   ├── PROTOCOL.md        Layer-2 wire format & handshake
│   ├── REGISTRY.md        Keys, Merkle log, revocation — the offline trust chain
│   ├── CONFORMANCE.md     What "Starboard Certified™" means, precisely
│   └── STRATEGY.md        Market, IP & licensing — incl. "why forking loses"
├── core/include/starboard/   Header-only C++17 reference implementation
│   ├── canon.hpp          Canonical constants, lattice, pinned exp/sincos
│   ├── id.hpp             Canonical identity (root of the pair transform)
│   ├── geometry.hpp       Layer 0: encounter kernel
│   ├── kepler.hpp         Layer 0: canonical two-body ephemeris
│   ├── screening.hpp      Layer 0: canonical conjunction search
│   ├── covariance.hpp     Layer 0: Starboard-Pc
│   ├── rules.hpp          Layer 1: the Rules of the Orbital Road
│   ├── trust.hpp          Adversarial model: assertions & downgrades
│   ├── registry.hpp       Offline trust chain (Merkle log, revocation)
│   ├── crypto/            SHA-512 + Ed25519 (verify-only), dependency-free
│   ├── frames.hpp         Hill frame, Clohessy-Wiltshire dynamics
│   ├── maneuver.hpp       Min-Δv give-way, mirror+phase return
│   └── multibody.hpp      Multi-craft window resolution
├── conformance/           Vectors SB-001..SB-018 + bit transcript (normative)
├── tools/bitcheck.sh      SB-C2 cross-toolchain bit-agreement matrix
├── examples/crossing_demo.cpp
└── CMakeLists.txt
```

The core is header-only, dependency-free, allocation-free, exception-free,
and avoids every source of cross-platform float nondeterminism on the
verdict path — written to be portable to a radiation-hardened flight MCU.

## How the author is paid — forever

Open-core / dual-license (see `docs/STRATEGY.md`):

- The **specification** is free (CC BY 4.0) — that's how it becomes *the*
  standard.
- The **reference implementation** is AGPL-3.0; commercial flight/ground
  software buys a per-fleet commercial license (`COMMERCIAL-LICENSE.md`).
- The **`Starboard Certified™`** mark + conformance suite is licensed
  per-constellation, renewably — and it is the enforcement layer that makes
  "compatible" mean something.

Adoption of the standard drives demand for the license. The same properties
that make the protocol safe make it unforkable.

## Status

Early. Private while the specification and IP position are established.
Ruleset SB/1 is implemented, proven, and conformance-tested;
`docs/DESIGN.md` Appendix B records everything that changed from v0.1.

## License

Reference implementation: **AGPL-3.0-or-later** (`LICENSE`), commercial
licensing available (`COMMERCIAL-LICENSE.md`). Specification text
(`docs/`): **CC BY 4.0**. No outside contributions without a CLA.
