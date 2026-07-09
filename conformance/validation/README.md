# Pc validation (SB-C3 accuracy evidence)

An **independent** check of Starboard-Pc's *physical accuracy* — separate from
the bit-determinism guarantee (SB-C2). Determinism means both craft compute
the identical Pc bits; accuracy means those bits are close to the true
probability of collision. This directory validates the latter.

- `pc_reference.py` — a reference Pc computed by a *different* method
  (encounter-plane projection re-derived from the spec, DESIGN.md §4.4, then
  the collision integral by scipy adaptive 2-D quadrature over the hard-body
  disc). Deliberately not the reference implementation's algorithm, so
  agreement validates the implementation rather than restating it.
- `pc_dump.cpp` — emits `starboard_pc` over a parameter grid (miss distance,
  covariance sigma/anisotropy/correlation, hard-body radius, relative-velocity
  direction) as CSV for the reference to score.

Run:

```
g++ -std=c++17 -ffp-contract=off -O2 -Icore/include \
    conformance/validation/pc_dump.cpp -o /tmp/pc_dump && /tmp/pc_dump > /tmp/pc.csv
python3 conformance/validation/pc_reference.py /tmp/pc.csv
```

The reference reports the maximum relative error over cases with meaningful Pc
(> 1e-6) and fails if any exceeds the documented bound.

## Result: Patera replaced Chan

This validator drove a Pc-method change. It first established that Chan's
equivalent-area series (the earlier kernel) is method-limited on **elongated /
correlated** covariance — the *common* LEO case, where along-track uncertainty
dominates — with 0.6–0.8% typical and up to ~15% relative error. The cause is
the circularization, not the series order (raising it from 24 to 80 terms left
the error unchanged), so a series variant could not fix it.

The disc integral is now **Patera's divergence-theorem contour integral**
(`PATERA_NODES = 32`, periodic trapezoid; see `covariance.hpp`). Its accuracy
is set by the node count, not the covariance shape, so it is accurate for any
aspect ratio while staying deterministic (pinned nodes; only `canon_exp` and
`canon_sincos`). Measured against this independent reference:

| Pc kernel | max rel. error (meaningful Pc), incl. 5:1–50:1 elongated |
|---|---|
| Chan series (old) | ~1.5e-1 worst, ~6e-3 typical — **fails** the 1e-4 bar |
| Patera N=32 (now) | **3.8e-6** worst (at Pc≈1e-12); ~1e-13 at meaningful Pc — **passes** |

The bound is currently **1e-4** (passed with orders of margin). Selection
evidence: a four-method bake-off (Patera, direct 2-D quadrature, Foster with a
pinned erf, corrected-Chan) measured all candidates against scipy adaptive
quadrature; Patera won on accuracy-for-any-covariance, cost (~32 nodes vs 2048
for direct quadrature), and determinism.
