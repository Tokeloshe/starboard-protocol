# Starboard — The Rules of the Orbital Road

*Design specification, v0.1 (draft).*

The design goal is **elegance through determinism**: two satellites facing a
close approach must reach the *same* decision about who gives way using only
information both can independently know — even with no communication between
them. Communication, when available, only *confirms* what the rules already
decided; it never changes the outcome for the simple case.

This mirrors maritime COLREGS. A skipper does not radio to ask "should I give
way?" — the geometry answers it. Starboard makes orbit behave the same way.

---

## 1. Three layers

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 3 — RETURN TO SLOT   flock back to station after clear │
├─────────────────────────────────────────────────────────────┤
│  Layer 2 — HANDSHAKE        optional intent confirmation      │
├─────────────────────────────────────────────────────────────┤
│  Layer 1 — RULES OF THE ROAD  deterministic give-way verdict  │
├─────────────────────────────────────────────────────────────┤
│  Layer 0 — CONJUNCTION GEOMETRY  TCA, miss distance, screening │
└─────────────────────────────────────────────────────────────┘
```

Layers 0 and 1 require **no communication**. That is the heart of the system.
Layers 2 and 3 are enhancements.

## 2. Layer 0 — Conjunction geometry

Each satellite continuously screens known nearby objects. Given the relative
position `dr = r_B − r_A` and relative velocity `dv = v_B − v_A` (from a shared
catalog, on-board ranging, or ISL exchange), assuming locally-linear motion over
the short screening horizon:

- **Time of closest approach:** `t_ca = −(dr · dv) / (dv · dv)`
- **Miss vector at TCA:** `miss = dr + dv · t_ca`
- **Miss distance:** `d_miss = ‖miss‖`

A **conjunction** is declared when `t_ca ∈ (0, horizon]` and `d_miss < R_screen`.
`R_screen` is a published constant (default 1 km hard-body + covariance margin).
Both satellites compute identical `t_ca`, `miss`, and `d_miss` from the same
relative state — the inputs are symmetric, so the verdict can be too.

*(Implemented in `core/include/starboard/geometry.hpp`, seeded from the SolarLens
orbital-mechanics `Vec3` and collision-screening code.)*

## 3. Layer 1 — The Rules of the Orbital Road

When a conjunction is declared, each satellite runs the **same** decision
function to produce a `GiveWayVerdict`: exactly one of the pair is the
**give-way** craft (it maneuvers); the other is the **stand-on** craft (it holds
its orbit, predictably, so the give-way craft can plan around it).

Determinism is guaranteed by a strict **lexicographic priority tuple**. The
craft with the *lower* priority gives way. Compared in order, first difference
decides:

1. **Rule 1 — Capability (a disabled craft stands on).**
   A satellite that *cannot* maneuver — no propellant, failed thruster, tumbling,
   or declared "not under command" — has right of way. The able craft must avoid
   it. *(COLREGS Rule 18: give way to vessels not under command.)* This is the
   single most important rule and it is asymmetric by physical fact, not
   convention.

2. **Rule 2 — Published criticality tier.**
   Each craft advertises a static, registry-backed tier (e.g. crewed > critical
   infrastructure > operational > deorbiting). Higher tier stands on. Tiers are
   public and immutable per mission phase, so both craft agree.

3. **Rule 3 — Overtaking gives way.**
   If one craft is overtaking the other (approaching from behind, i.e. `dv` is
   nearly anti-parallel to the stand-on craft's velocity within a defined cone),
   the overtaking craft gives way. *(COLREGS Rule 13.)* Geometry both can compute.

4. **Rule 4 — The starboard rule (crossing).**
   For a genuine crossing, the craft that has the other on its **starboard side**
   gives way — "starboard" defined by a published body frame convention (velocity
   vector = forward, nadir = down, starboard = forward × down). Both compute the
   same handedness from the same relative geometry. *(COLREGS Rule 15 — the
   namesake.)*

5. **Rule 5 — Deterministic tie-break.**
   If all else is symmetric, the craft with the **higher** canonical object ID
   (COSPAR/NORAD, compared as a fixed-width string) gives way. This guarantees a
   unique answer in *every* case with zero ambiguity — the property manual
   coordination can never promise.

Because every rule is computed from symmetric, publicly-known inputs, **both
satellites independently reach the identical verdict**. No message is required
for correctness.

### The give-way maneuver
The give-way craft computes the minimum-Δv burn that raises `d_miss` above the
safe threshold `R_safe` at `t_ca` — preferring an **in-track/radial** nudge that
is cheap to later undo. The stand-on craft does nothing (predictability *is* its
contribution). Both is-safe checks use the same `R_safe` constant.

## 4. Layer 2 — The handshake (optional)

When the pair *can* communicate (same ground provider, or a direct inter-satellite
link), they exchange three tiny messages to confirm and optimize — never to
override Layer 1:

```
INTENT   give-way craft → stand-on craft:  "conjunction <id>, TCA <t>, I give way, Δv=<v>, dir=<û>"
ACK      stand-on craft → give-way craft:  "acknowledged, I stand on"
CLEAR    give-way craft → stand-on craft:  "maneuver complete, d_miss now <d>, returning to slot"
```

If Layer 1 ever produced disagreement (it should not, but comms let us verify),
the handshake surfaces it and both fall back to the deterministic tuple, logging
the discrepancy for the conformance suite. See `docs/PROTOCOL.md` for the wire
format.

## 5. Layer 3 — Return to slot (the flock)

After `CLEAR`, the give-way craft executes a minimum-energy return to its assigned
station using the SolarLens formation station-keeping controller (PD control on
position error, drift-tolerance dead-band). The emergent behaviour across a dense
constellation is exactly the murmuration image: a threat ripples through, a few
craft peel off to give way, then the whole formation settles back into its slots
in unison — no central choreographer.

This layer is a thin adaptation of `SwarmFormation::station_keeping_step()` and
`resolve_collisions()` from SolarLens.

## 6. What is fixed by the spec (so implementations agree)

Published constants that all conformant implementations MUST share:

| Symbol | Meaning | Default |
|--------|---------|---------|
| `R_screen` | screening radius to declare a conjunction | 1000 m + covariance |
| `R_safe`   | required miss distance after give-way | 500 m |
| `horizon`  | forward screening window | 3 orbital periods |
| overtaking cone | half-angle defining "overtaking" | 67.5° (COLREGS analog) |
| body frame | forward / down / starboard convention | velocity / nadir / fwd×down |
| tie-break  | canonical ID ordering | COSPAR asc., higher gives way |

Everything else (sensor fusion, catalog source, propulsion) is implementation-defined.

## 7. Mapping to seeded SolarLens code

| Starboard component | SolarLens origin |
|---------------------|------------------|
| `Vec3`, orbital state | `physics/orbital_mechanics.hpp` |
| conjunction screening (`check_collisions`) | `spacecraft/formation_control.cpp` |
| give-way / resolve maneuver | `SwarmFormation::resolve_collisions()` |
| return-to-slot | `SwarmFormation::station_keeping_step()` |
| shared constants | `physics/constants.hpp` |

## 8. Open questions for v0.2

- Covariance-aware screening (probability of collision, not just miss distance).
- Multi-body conjunctions (3+ craft in one screening window) — pairwise resolution
  order must itself be deterministic.
- Trust / spoofing: how does a craft trust an advertised "I cannot maneuver"?
  (Signed capability assertions; conservative default = treat unknowns as stand-on.)
- Fuel-fairness over repeated encounters (should the same craft always give way?).
