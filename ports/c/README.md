# Starboard — C99 port

The **independent second implementation** of the Starboard verdict path, in
MISRA-oriented C99 — the flight language of the CubeSat / rad-hard-MCU /
NASA-cFS world.

Its job is twofold:

1. **Portability** — run the verdict path where C++17 doesn't go: no VLAs,
   no dynamic allocation, no recursion, fixed-size arrays, explicit-width
   types, braces on everything.
2. **Proof** — demonstrate that the *specification*, not a compiler accident,
   defines the bits. This port reproduces the C++ reference's `--bits`
   transcript **byte-for-byte**; the full matrix (both languages × gcc/clang
   × −O0…−O3 −march=native = 29 builds) collapses to a single hash.

```
make            # build sb_bits (the transcript emitter)
make check      # diff its output against ../../conformance/expected_bits.txt
make CC=clang   # clang honors the in-source FP_CONTRACT pragma too
```

## What is (and isn't) here

| Ported (bit-checked) | Not ported (C++ reference only) |
|---|---|
| `canon.c` — lattice, `canon_exp`, `canon_sincos` | Ed25519 verification (`crypto/ed25519_verify.hpp`) |
| `geometry.c` — Vec3, encounter kernel | CW maneuver planning (`frames.hpp`, `maneuver.hpp`) |
| `propagator.c` — J2 RK4, signed fixed step | broad-phase (`broadphase.hpp`) |
| `covariance.c` — Patera Pc contour | |
| `screening.c` — canonical conjunction search | |
| `rules.c` — the Rules of the Orbital Road | |
| `sha512.c`, `registry.c` — hash chain / Merkle | |

## Determinism notes (read before touching anything)

- **FP contraction must stay off.** A fused multiply-add changes verdict-path
  bits. `starboard.h` pins `#pragma STDC FP_CONTRACT OFF` in-source (honored
  by clang — verified to hold even under `-mfma`); GCC's C frontend ignores
  that pragma, so GCC builds **must** pass `-ffp-contract=off` (the Makefile
  does). See `docs/DESIGN.md` §3.5.
- **Operation order is normative.** Dot products expand `(x·x′ + y·y′) + z·z′`;
  Horner polynomials and summations run in the pinned order. Reassociating
  *anything* on the verdict path is a conformance break, not a refactor.
- **Only two transcendentals** are permitted on the verdict path — the ported
  `canon_exp` and `canon_sincos`. `sqrt`, `ldexp`, `nearbyint` from libm are
  exact and allowed. libm `sin/cos/exp/pow/atan2/erf` are not.
- MISRA deviations are documented in-file where they occur (e.g. `printf` in
  the host-side transcript emitter, which is tooling, not flight code).
