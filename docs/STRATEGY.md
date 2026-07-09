# Starboard — Strategy, IP & Licensing

*How a solo author turns a space-traffic convention into a durable,
licensable asset.*

This document is deliberately honest. There is no passive "publish it and
the money arrives" path. But there **is** a well-worn path where a
widely-adopted open standard forces a stream of commercial-license and
certification revenue back to whoever controls the reference implementation
and the trademark. That is the goal, and it is achievable by one person —
*because the defensibility is engineered into the protocol itself* (§5),
not bolted on with lawyers.

---

## 1. The opportunity

| Fact | Implication |
|------|-------------|
| ~10,000 active satellites in 2026, tracking toward 100,000+ this decade. | Pairwise conjunction events grow with the **square** of population. Manual coordination collapses. |
| Cross-operator collision avoidance is resolved today by *email and shared spreadsheets*. | There is no machine-to-machine convention. A gaping, unglamorous, universal gap. |
| SpaceX already auto-avoids — but only *within* its own fleet, with proprietary logic. | Nobody owns the *between-operators* layer. |
| Orbital data-centre constellations (Starcloud flew an H100 in 2025; Google's Project Suncatcher; Amazon and others) are coming. | Dense, high-value, formation-flying clusters that make coordinated give-way + return-to-slot mission-critical — Starboard's exact wheelhouse. |
| Regulators (FCC, ESA, UN COPUOS) are pushing "coordinated / automated" conjunction management. | A neutral, open convention is what they *want* to endorse. The first credible one wins mindshare. |

**The thesis:** the deconfliction layer between different operators'
satellites is the "TCP/IP of space traffic." It must be an open standard to
be adopted, and whoever authors the canonical spec + reference
implementation + conformance mark sits at a toll booth on that adoption.

## 2. Competitive landscape (honest)

- **LeoLabs** — radar tracking + conjunction *data*. Does not define an
  on-board give-way convention. → *Customer/partner, not competitor.*
- **Slingshot Aerospace (Beacon)** — operator-to-operator coordination
  platform. Closest in spirit, but **centralized and commercial**, not an
  open, offline-capable convention in flight software. → *The incumbent to
  differentiate against: Starboard works when the platform is unreachable,
  and Beacon could carry Starboard messages.*
- **Kayhan Space (Pathfinder/Satcat)** — collision-avoidance **service**.
  A SaaS, not a shared ruleset.
- **CARA/NASA, 18th SDS/Space-Track** — government conjunction data.
  Data, not conventions.

**The white space:** nobody owns the *open, deterministic, comms-optional
right-of-way convention* — the thing that works between two satellites that
have never heard of each other's company. Standard-shaped, durable,
unclaimed.

## 3. Why "open" is the point (and not giving it away)

A collision-avoidance convention is worthless if only one fleet follows it.
Its value is **purely a function of adoption**. Therefore the specification
is free (CC BY 4.0) so it can become *the* standard — and the money is in
what adoption requires: implementation (dual-licensed code) and proof of
compatibility (certification). Openness is not charity; it is the moat's
construction method.

## 4. The revenue model — three renewable streams

### Stream 1 — Dual-license the reference implementation (primary)
Reference code is **AGPL-3.0-or-later**. Operators will not open their
flight software, so they buy a **commercial license** (per-fleet, annual)
that waives copyleft. The MongoDB/Qt playbook; recurs because licenses are
per-fleet and time-bound, and every new constellation is a new customer.
The reference implementation is deliberately *worth licensing*: header-only
C++17 (Starlink-class), with a MISRA-C99 port (CubeSat / rad-hard-MCU / NASA
cFS class) in preparation as an independent, bit-for-bit second
implementation, dependency-free, allocation-free, with the bit-determinism
work (quantization lattice, pinned transcendentals, strict arithmetic
profile, cross-compiler bit-agreement) already done — months of specialist
effort a vendor would rather buy than replicate.

### Stream 2 — `Starboard Certified™` conformance (durable + defensible)
Trademark + the conformance suite ([`CONFORMANCE.md`](CONFORMANCE.md)).
"Compatible" has a precise, testable, owned definition (SB-C1/C2/C3);
operators pay for certification and the right to display the mark; insurers
and licensing bodies are lobbied to *recognize* it. Trademarks don't expire
while used and defended — this is the forever stream, and it is the
enforcement layer for §5.

### Stream 3 — Registry & tooling services (later)
The capability-assertion registry (DESIGN.md §8.2) is a natural hosted
service: key management, tier attestations, dispute logs, hosted
conformance runs. Pursue only after the standard has traction.

### On patents (proceed with caution)
Copyright (dual-license) + trademark (certification) first. A narrow
defensive provisional on the handshake only once there is commercial
traction. Do **not** gate the spec behind a patent — that kills adoption
and the whole thesis.

## 5. Why forking loses — the defensibility argument

The obvious attack: "it's open — a big operator reads the spec, tweaks 10%,
ships their own." The protocol is engineered so that the tweak is a bug,
not a differentiator:

1. **The interoperability trap.** Change any normative value — a constant,
   a dead-band, the rule order, the Pc series — and against the installed
   base the fork sometimes computes the *opposite* give-way verdict. A fork
   doesn't get a slightly different standard; it gets **non-deterministic
   behaviour against every compliant craft**, which is the precise failure
   the protocol exists to eliminate. Deviation strictly destroys value for
   the deviator (DESIGN.md §11, CONFORMANCE.md §6).
2. **Off-network at the identifier layer.** The ruleset version is hashed
   into every conjunction id (DESIGN.md §9.3). A fork does not merely
   disagree about verdicts — it cannot even *name encounters* consistently
   with compliant craft, and every compliant stack discards its handshake
   traffic on the `ruleset_id` check.
3. **"Compatible" is owned.** The conformance suite + mark give
   compatibility a testable definition under the author's trademark. The
   fork's trilemma: pass the suite (it isn't a fork), fail it (it can't
   claim compatibility or interoperate), or fork the suite too (a new,
   empty network competing against an installed base from zero — see point
   1 for how that goes).
4. **The only rational "fork" is a rival standard from scratch** — a
   standards war, fought against an incumbent that is free, neutral,
   better-engineered (three proven theorems, bit-level determinism,
   conformance vectors) and already flying. Mitigation for even that: be
   first, be neutral, get a data provider emitting Starboard-format
   conjunctions early.

Note what does the work here: **the same properties that make the protocol
safe (determinism, shared constants, canonical ids) are the properties that
make it unforkable.** Engineering rigor and defensibility are one artifact.

## 6. Adoption path (the hard part, sequenced)

1. **Publish spec + reference impl** openly; git history + license notices
   are the provenance record. *(done — this repo)*
2. **Ship the irrefutable demo**: two rival sats, zero comms, identical Pc
   bits, identical verdict, min-Δv burn, flock back to slot; then a 3-body
   window. `examples/crossing_demo.cpp` prints exactly this timeline — a
   60-second screen recording is the pitch.
3. **One lighthouse adopter** — a cubesat operator or university mission —
   flies or sim-flies it. One real logo breaks the chicken-and-egg.
4. **Court a data provider** (LeoLabs-type) to emit Starboard-format
   conjunction messages: their data + this convention = a complete loop.
5. **Lobby the mark** into an insurer questionnaire or licensing checklist.
   The flywheel: recognition → soft mandate → certification revenue →
   more recognition.
6. **Standards body** (CCSDS / ISO / IADC): contribute the spec text while
   retaining the trademark + reference implementation — the classic control
   point. The `RULESET_ID` mechanism means even a standards-body variant
   must be an explicit, coordinated migration, not a silent divergence.

## 7. Risks & honest caveats

- **Standards adoption is slow and political.** Years. Most standards die.
- **A big operator could publish a rival open spec.** See §5.4 mitigation:
  first, neutral, better-engineered, data-provider allies.
- **AGPL enforcement requires vigilance.** It is a licensing *business*,
  lightweight but real.
- **Solo-author bus factor.** The conformance suite mitigates: the standard
  is executable, so it survives its author's attention span.
- **Productize the licensing, not yourself.** Off-the-shelf dual-license
  flow, standard trademark registration, templated certification
  engagements. The engineering asset is the thing only you can make.

## 8. What to protect, starting now

- [x] Authorship provenance — private repo, dated history, SPDX headers.
- [x] Conformance suite as a first-class deliverable (the enforcement layer).
- [ ] Reserve name/domain; file the wordmark (software; testing/certification
      services). Verify "Starboard" availability; alternates: *Murmuration*,
      *Concord*, *Rightway*.
- [ ] Keep sole copyright until a standards contribution is deliberately
      chosen. **No outside code without a CLA** — otherwise dual-licensing
      dies.
- [ ] Registry key ceremony design before any flight use of signed
      assertions (DESIGN.md §8.2).
