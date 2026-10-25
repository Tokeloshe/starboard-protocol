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

## What is here

This is no longer only a verdict-path port: it is the **complete C99 flight
library** (`libstarboard.a`, see [`docs/FLIGHT.md`](../../docs/FLIGHT.md)).

| Layer | Files | Cross-checked against C++ by |
|---|---|---|
| Canon, geometry, J2 propagator, Pc, search, rules | `canon.c` `geometry.c` `propagator.c` `covariance.c` `screening.c` `rules.c` | v1 transcript (normative) |
| CW maneuver + returns, multi-body cascade, broad-phase | `maneuver.c` `multibody.c` `broadphase.c` | v2 transcript |
| CDM binding (exact decimal ingest) | `cdm.c` | v2 transcript + differential fuzzer |
| SHA-512, Ed25519 (32-bit limbs), registry pipeline | `sha512.c` `ed25519.c` `registry.c` | v1 transcript, `tests/diff_ed25519.cpp`, Wycheproof |
| Flight layer, CCSDS telemetry | `fsw.c` `ccsds.c` | — (C only; C ABI for C++ hosts) |
| The flock (Layer 4) | `flock.c` | — |
| Correctly-rounded soft double for FPU-less ARM | `softdouble.c` | x86-64 hardware, 49 M cases |

```
make check                 # v1 + v2 transcripts, self-test, flight/flock tests
make -C tests all-tests    # crypto: RFC 8032, Wycheproof, C-vs-C++ differential
./sbctl cdm ../../examples/cdm/shell_vs_debris.cdm --verified
./murmuration              # the flock demo
```

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
