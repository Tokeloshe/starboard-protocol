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

## Known result driving the current Pc rework

This validator established that Chan's equivalent-area series (the earlier Pc
kernel) is method-limited on **elongated / correlated** covariance — the
*common* LEO case, where along-track uncertainty dominates. The error is in
the circularization, not the series order (raising the order from 24 to 80
terms leaves it unchanged), so the disc integral is being replaced by a
method that is accurate for any covariance aspect ratio while remaining
deterministic (pinned node count, only `canon_exp`/`canon_sincos`). This
README and the bound are updated when that rework lands.
