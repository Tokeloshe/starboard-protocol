# Starboard — Strategy, IP & Licensing

*How a solo author turns a space-traffic convention into a durable, licensable
asset.*

This document is deliberately honest. There is no passive "publish it and the
money arrives" path. But there **is** a well-worn path where a widely-adopted
open standard forces a stream of commercial-license and certification revenue
back to whoever controls the reference implementation and the trademark. That is
the goal, and it is achievable by one person.

---

## 1. The opportunity

| Fact | Implication |
|------|-------------|
| ~10,000 active satellites in 2026, tracking toward 100,000+ this decade. | Pairwise conjunction events grow with the **square** of population. Manual coordination collapses. |
| Cross-operator collision avoidance is resolved today by *email and shared spreadsheets*. | There is no machine-to-machine convention. A gaping, unglamorous, universal gap. |
| SpaceX already auto-avoids — but only *within* its own fleet, with proprietary logic. | No operator's system interoperates with another's. Nobody owns the *between-operators* layer. |
| Orbital data-centre constellations (Starcloud flew an H100 in 2025; Google's Project Suncatcher; Amazon/others) are coming. | These are dense, valuable, formation-flying clusters that make coordinated station-keeping mission-critical — Starboard's exact wheelhouse. |
| Regulators (FCC, ESA, UN COPUOS) are actively pushing "coordinated / automated" conjunction management. | A neutral, open convention is what they *want* to endorse. First credible one wins mindshare. |

**The thesis:** the deconfliction layer between different operators' satellites
is the "TCP/IP of space traffic." It must be an open standard to be adopted, and
whoever authors the canonical spec + reference implementation + conformance mark
sits at a toll booth on that adoption.

## 2. Competitive landscape (honest)

- **LeoLabs** — ground-based radar tracking + conjunction *data*. Sells the
  catalog and warnings. Does **not** define an on-board give-way convention.
  → *Potential customer/partner, not a direct competitor.*
- **Slingshot Aerospace** — space-traffic coordination platform, "Beacon" for
  operator-to-operator coordination. Closest in spirit. But it is a
  **centralized commercial platform**, not an open, offline-capable convention
  baked into flight software. → *The incumbent to differentiate against: we are
  the open, decentralized, no-comms-required standard they can even build on.*
- **Kayhan Space (Pathfinder / Satcat)** — autonomous collision-avoidance
  **service** for operators. Again a service/SaaS, not a shared ruleset.
- **CARA / NASA, 18th SDS / Space-Track** — government conjunction data services.
  Data providers, not conventions.

**The white space:** nobody owns the *open, deterministic, comms-optional
right-of-way convention* — the thing that works even when the two satellites
have never heard of each other's company. That is the durable, standard-shaped
position, and it is unclaimed.

## 3. Why "open" is the point (and not giving it away)

A collision-avoidance convention is worthless if only one fleet follows it — you
cannot deconflict against a satellite that plays by different rules. Its value is
**purely a function of adoption**. Therefore:

- The **specification** must be free and openly published (CC BY 4.0). This is
  what lets it become a de-facto and eventually de-jure standard.
- The **money** is not in the spec. It is in what adoption *requires*:
  implementation and certification.

## 4. The revenue model — three renewable streams

### Stream 1 — Dual-license the reference implementation (primary)
- Reference code is **AGPL-3.0-or-later**. AGPL's strong copyleft means any
  organization that links it into proprietary flight or ground software, or
  offers it as a service, must open **their** source too.
- Satellite operators will not open their flight software. So they buy a
  **commercial license** (per-fleet, annual) that waives the AGPL obligation.
- This is exactly the MongoDB / Qt / MySQL / BusyBox playbook: give the standard
  away, sell the exception. Revenue recurs because licenses are per-fleet and
  time-bounded, and because every new constellation is a new customer.

### Stream 2 — `Starboard Certified™` conformance (durable + defensible)
- Register the trademark. Publish a conformance **test suite** (spec-defined
  scenarios; a compliant stack must produce the spec's exact give-way verdicts).
- Operators pay for certification + the right to display the mark. Insurers and
  launch-licensing bodies are lobbied to *recognize* the mark → it becomes a
  soft requirement → certification becomes non-optional.
- Trademark protection does not expire as long as it is used and defended. This
  is the "forever" stream — it outlives any single patent.

### Stream 3 — Reference data & tooling services (optional, later)
- Hosted conformance testing, a maneuver-negotiation broker/relay, priority-tier
  registry, telemetry adapters. SaaS layered on the free standard. Scales but
  needs ops — pursue only after the standard has traction.

### On patents (proceed with caution)
- A narrow patent on the *specific negotiation handshake* (Stream 1 of the
  protocol) could become standards-essential and FRAND-licensable. But patents
  are slow (2–4 yrs), expensive ($15–30k+ per jurisdiction), and a patent on a
  *standard* invites hostility. **Recommendation:** rely on copyright
  (dual-license) + trademark (certification) first. Consider a defensive
  provisional patent on the handshake only once there is commercial traction to
  justify the cost. Do **not** gate the free spec behind a patent — that kills
  adoption and the whole thesis.

## 5. Adoption path (the hard part, sequenced)

1. **Publish the spec + reference impl** openly. Establish authorship and date
   (this repo's git history + the CC BY / AGPL notices are the provenance record).
2. **Ship an irrefutable demo** (see `examples/`): two "rival-operator" sats,
   no shared comms, independently computing the same give-way verdict and
   settling back into formation. A 60-second video of this is the pitch.
3. **Get one lighthouse adopter** — a cubesat operator or university mission —
   to fly or sim-fly it. One real logo breaks the chicken-and-egg.
4. **Court a data provider** (LeoLabs-type) to emit Starboard-format conjunction
   messages: their data + our convention = complete loop, and it validates us.
5. **Lobby the mark** into an insurer's questionnaire or a national licensing
   checklist. This is the flywheel: recognition → soft-mandate → certification
   revenue → more recognition.
6. **Standards body** (CCSDS / ISO / IADC working group). Contributing the spec
   while retaining the trademark + reference impl is the classic control point.

## 6. Risks & honest caveats

- **Standards adoption is slow and political.** Years, not months. The IP is only
  worth something if adoption happens; most standards die.
- **A big operator could publish a rival open spec.** Mitigation: be first, be
  neutral, be genuinely better-engineered, get a data provider on board early.
- **AGPL enforcement requires vigilance** (and occasionally lawyers). It is not
  passive income; it is a licensing *business*, lightweight but real.
- **You are not a businessperson — so productize the licensing, not yourself.**
  Use an off-the-shelf dual-license/commercial-license flow, a standard trademark
  registration, and a simple certification checklist. The engineering asset (this
  repo) is the thing only you can make; outsource or template the paperwork.

## 7. What to protect, starting now

- [x] Establish authorship provenance — private repo, dated git history, license
      headers. *(this commit)*
- [ ] Reserve the name/domain and file the `Starboard` (or final chosen name)
      wordmark in the relevant classes (software; testing/certification services).
- [ ] Keep the spec and reference impl under your sole copyright until/unless a
      standards body contribution is deliberately chosen.
- [ ] Do not accept outside code contributions without a CLA (contributor license
      agreement) that assigns/licenses rights to you — otherwise you lose the
      ability to dual-license.

> **Name note:** "Starboard" is a strong working name (nautical give-way + "star").
> Verify trademark availability before committing; alternates: *Murmuration*,
> *Concord*, *Waypoint*, *Rightway*.
