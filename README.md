# Starboard

**The Rules of the Orbital Road.**

An open, deterministic protocol that lets any two satellites — even from rival
operators, even with no radio contact — independently agree on *who gives way*
before a close approach, execute the avoidance, and then flock back to their
assigned slots. No ground station. No human in the loop. No emails between
operators.

At sea this problem was solved 150 years ago. When two vessels cross, the one
that has the other on its **starboard** side gives way, alters course to
starboard, and passes behind. Both skippers compute the same answer from the
same geometry — no negotiation required. That single elegant convention (COLREGS)
is why millions of ships share the water without a central traffic controller.

Low Earth orbit has no such convention. There are ~10,000 active satellites
today and credible plans for 100,000+ (Starlink, Kuiper, Guowang, and the new
wave of orbital data-centre constellations). Collision avoidance today is done
by each operator privately, with cross-operator conflicts resolved by *email*.
That does not scale. Starboard is the missing convention.

---

## What Starboard is

1. **A ruleset** — "the Rules of the Orbital Road." A small, fully deterministic
   set of right-of-way rules. Given a predicted conjunction, both satellites
   compute the *identical* answer for who gives way, with zero communication.

2. **A negotiation handshake** — an optional, lightweight message exchange for
   when the two craft *can* talk (shared ground provider, inter-satellite link),
   to confirm intent and de-conflict maneuver magnitude.

3. **A return-to-slot behaviour** — after giving way, a minimum-energy return to
   assigned station, so a constellation behaves like a flock of birds: perturbed
   by a threat, then settling back into formation in unison.

4. **A reference implementation** — portable, dependency-free C++ (this repo),
   built on flight-proven orbital-mechanics and formation-control code.

## Why it becomes integral

- **Interoperability standard, not a product.** The value is that *everyone*
  runs the same rules. Like TCP/IP, a partially-adopted standard is worth little
  and a widely-adopted one is worth everything — adoption is the moat.
- **Regulator tailwind.** The FCC, ESA and UN COPUOS are all pushing toward
  automated, coordinated conjunction management. A neutral open standard is what
  they want to point to.
- **Insurer / launch-license leverage.** "Starboard-compliant" becomes a checkbox
  that lowers premiums and eases licensing — a soft mandate that pulls adoption.

## How the author is paid — forever

Starboard uses an **open-core / dual-license** model (see
[`docs/STRATEGY.md`](docs/STRATEGY.md)):

- The specification is open and free, so it can become *the* standard.
- The reference implementation is **AGPL-3.0**. Anyone may use it — but flight
  and commercial ground software that doesn't want AGPL's source-disclosure
  obligation must buy a **commercial license** from the author.
- The **`Starboard Certified™`** trademark + conformance test suite is licensed
  per-constellation, renewably.

This is the same mechanism that pays the authors of Qt, MongoDB and BusyBox.
Adoption of the *standard* drives demand for the *license*.

## Repository layout

```
starboard-protocol/
├── README.md              You are here
├── docs/
│   ├── STRATEGY.md        Market, IP & licensing — the business case
│   ├── DESIGN.md          The Rules of the Orbital Road, in full
│   └── PROTOCOL.md        Wire format & the negotiation handshake
├── core/                  Reference implementation (C++17, no deps)
│   ├── include/starboard/
│   │   ├── geometry.hpp   Vec3 + conjunction geometry (TCA, miss distance)
│   │   └── rules.hpp      The deterministic give-way ruleset
│   └── src/rules.cpp
├── examples/
│   └── crossing_demo.cpp  Two rival-operator sats de-conflict a crossing
└── CMakeLists.txt
```

## Status

Early. This repo is **private** while the specification and IP position are
established. Design and strategy first; reference code seeded from the SolarLens
formation-control and orbital-mechanics modules.

## License

Reference implementation: **AGPL-3.0-or-later** (see [`LICENSE`](LICENSE)).
Commercial licensing: see [`COMMERCIAL-LICENSE.md`](COMMERCIAL-LICENSE.md).
Specification text: CC BY 4.0.
