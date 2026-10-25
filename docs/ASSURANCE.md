# Starboard — assurance case

*Every claim the README makes, the evidence for it, and the one command that
re-creates that evidence. If a row cannot be reproduced from a clean
checkout, that is a bug: please report it.*

| # | Claim | Evidence | Reproduce |
|---|---|---|---|
| A1 | Both craft compute the **same verdict** (Theorem 1) | CBMC proof for every tier/trust/capability, id pair, epoch and predicate function; 4 096-pair swap sweep (SB-012) | `make -C verification/cbmc theorem1 cid` |
| A2 | The verdict is **bit-identical across compilers and languages** | 58 transcripts (v1 + v2), C++17 and C99, gcc/clang, -O0…-O3 -march=native | `tools/bitcheck.sh` |
| A3 | …**across CPU architectures** | 11 architectures incl. RAD750-class PowerPC, RISC-V, SPARC, big-endian MIPS/s390x | `tools/crossarch.sh` · [CROSS-ARCH](evidence/CROSS-ARCH.md) |
| A4 | …**on bare-metal microcontrollers** | Cortex-M3, M4F, M7, no OS | `tools/baremetal.sh` |
| A5 | A platform that **would** disagree **refuses to fly** | x87 and FMA controls + the ARM soft-float finding: all caught by the self-test | `tools/crossarch.sh` (CONTROL/FINDING rows) |
| A6 | Conformance vectors pass | SB-001…SB-020 | `ctest --test-dir build` |
| A7 | Real **CCSDS CDMs** are ingested **identically** by independent implementations | C99 vs C++ bindings: v2 transcript (400 generated CDMs incl. 104 malformed) + differential fuzzer | `verification/fuzz/run.sh` |
| A8 | Decimal ingest is **exact** | 119 M fuzzed inputs equal glibc's correctly-rounded `strtod` | `verification/fuzz/run.sh` |
| A9 | Hostile **uplink / ISL input** cannot crash or corrupt | ASan+UBSan fuzzing (≈129 M executions) + CBMC proofs for every 204-byte notice frame and every CDM ≤ 40 B | `verification/fuzz/run.sh` · `make -C verification/cbmc notice cdm` · [FUZZING](evidence/FUZZING.md) |
| A10 | **Single-event upsets** are detected and contained | fault injection: single upsets corrected 2-of-3, triple upsets → FAULT, nothing commanded; RAM corruption of a stored CDM detected | `make -C ports/c test_fsw_fi && ports/c/test_fsw_fi examples/cdm` |
| A11 | Bad input **fails safe**, never "all clear" | NaN/Inf/out-of-domain rejection; NaN-arc hang fixed; non-finite Pc repair | `ports/c/test_fsw`, DESIGN §3.7 |
| A12 | Fits a **CubeSat MCU** | 43.2 KB code, 24.8 KB RAM, 6.4 KB worst-case stack, no heap, recursion-free (proved) | `tools/baremetal.sh` · FLIGHT §2 |
| A13 | **MISRA C:2012** | cppcheck MISRA addon: every finding fixed or a documented deviation | `tools/misra.sh` · [MISRA](evidence/MISRA.md) |
| A14 | The **flock** converges | 72-satellite shell: notice reaches all in 9 hops, all ledgers identical (digest); 8-craft formation: alerts raised by exactly the affected members | `make -C ports/c murmuration && ports/c/murmuration` |
| A15 | Notices are **predictable** from shared data | a neighbour holding the CDM recomputes the mover's notice bit-for-bit | `ports/c/test_fsw` ("silent flock") |
| A16 | Trust chain verifies **offline**, portably (no 128-bit types) | 122 crypto/registry tests (incl. 32-bit and sanitized), **210/210 Wycheproof** Ed25519 vectors in both implementations, 27 000-case C vs C++ differential, libsodium-signed registry fixtures | `make -C ports/c/tests all-tests` |
| A17 | The shared encounter geometry **is the provider's** (TCA anchoring) | the canonical integrator round-trips the full arc to < 1 mm (SB-014); the CDM binding back-propagates only 512 s | `ctest` (SB-014) · DESIGN §3.3a |
| A18 | Pc is **accurate** | Patera vs independent adaptive quadrature: ~1e-13 at meaningful Pc | `conformance/validation/` |

## What is *not* claimed

* **Flight heritage.** Nothing here has flown or run on physical flight
  hardware. QEMU models instruction semantics, not silicon errata.
* **Certification.** No independent V&V, no qualified MISRA tool, no
  DO-178C / ECSS-E-ST-40C / NASA-STD-8739.8 objectives claimed.
* **Real-time timing on target.** Budgets are measured for code size,
  memory and stack; cycle counts must be measured on the flight processor.
* **A high-fidelity truth-model validation** of the J2 canonical model and
  of maneuver efficacy over long lead times (OPEN-PROBLEMS #1, #6).
* **Upstream status of the libgcc finding.** Reproduced and characterized
  under emulation; silicon confirmation and an upstream report are open.

See [`OPEN-PROBLEMS.md`](OPEN-PROBLEMS.md) for the full ledger.
