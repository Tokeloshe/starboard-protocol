# Evidence — bit-agreement across CPU architectures and microcontrollers

Starboard's central claim is that two spacecraft compute **the same bits**.
The compiler × language matrix (`tools/bitcheck.sh`: 58 transcripts, C++ and
C, gcc and clang, -O0 … -O3 -march=native) shows it on one machine. This file
records the evidence across **machines**: flight-relevant CPU families, plus
bare-metal microcontrollers with no operating system.

Each target builds the C99 flight library with a real cross-compiler and
runs, under QEMU:

* the **v1 transcript**, normative: transcendentals, J2 ephemeris, conjunction
  search, Pc kernel, the 4 096-pair verdict population, registry hashing;
* the **v2 transcript**, informative: CW maneuvers, returns, multi-body
  windows, broad-phase sets, exact decimal ingest, the CDM binding over 400
  generated messages;
* the **flight self-test** (eleven known-answer tests);
* the **flight, flock and fault-injection test suites** (102 checks).

## Linux targets — `tools/crossarch.sh`

| Target | Flight class | v1 transcript | v2 transcript | Self-test | Flight + flock tests |
|---|---|---|---|---|---|
| x86-64 | Starlink-class Linux SBC / ground | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| x86 32-bit SSE2 | legacy 32-bit x86 | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| AArch64 (ARMv8-A) | Zynq UltraScale+ / Cortex-A53/A72 OBCs | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| ARMv7-A hard-float | Zynq-7000 CubeSat OBCs | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| PowerPC 32 big-endian (`-mcpu=750`) | RAD750-class | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| PowerPC64 LE | POWER-class ground segment | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| ARMv5 soft-float | FPU-less ARM, with `softdouble.c` | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| RISC-V 64 | NASA HPSC / NOEL-V class | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| SPARC64 big-endian | SPARC (LEON heritage ISA) | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| MIPS32 big-endian | MIPS (legacy avionics) | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| s390x big-endian | z/Architecture (IBM IEEE BFP) | ✅ identical | ✅ identical | ✅ pass | ✅ pass |
| **FINDING:** ARMv5 soft-float, stock libgcc | libgcc `__aeabi_dadd` misrounds at exponent gap 33 | ❌ diverges | ❌ diverges | 🛑 caught → SAFE | — |
| **CONTROL:** x87 excess precision | must fail bits *and* self-test | ❌ diverges | ❌ diverges | 🛑 caught → SAFE | — |
| **CONTROL:** FMA contraction on | must fail bits *and* self-test | ❌ diverges | ❌ diverges | 🛑 caught → SAFE | — |

Eleven architectures, both byte orders, 32- and 64-bit, hardware and software
floating point: **one set of bits.** The three non-conformant environments
all produce wrong bits **and are refused by the self-test**. That is the
property that matters: a craft that would disagree with its peers will not fly.

## Bare-metal microcontrollers — `tools/baremetal.sh`

No operating system: a vector table, newlib, ARM semihosting for the console,
QEMU MPS2 boards.

| MCU class | doubles | v1 transcript | v2 transcript | self-test | verdict |
|---|---|---|---|---|---|
| Cortex-M7 (FPv5 double FPU) | hardware | identical | identical | pass | CONFORMANT |
| Cortex-M4F (single-precision FPU) | software (`softdouble.c`) | identical | identical | pass | CONFORMANT |
| Cortex-M3 (no FPU) | software (`softdouble.c`) | identical | identical | pass | CONFORMANT |
| **FINDING:** Cortex-M4F, stock libgcc doubles | software (libgcc) | diverges | diverges | FAIL | refused by self-test (SAFE) |

The flight image on the M7 boots, passes every self-test, decides a CDM
(give way, 0.315 m/s, miss raised 140 m → 1 280 m) and emits its three
CCSDS packets, with no OS anywhere.

## Finding: ARM soft-float double addition misrounds

**Symptom.** On ARMv5 Linux (soft-float ABI) the v1 transcript's
`transcendental` fold differed from every other platform. Bisecting the
sweep isolated a single input, `canon_sincos(0.017 × −462)`, whose sine
differed by one ulp. Tracing every intermediate showed all operations
matched up to the last one: the cosine kernel's final `1.0 + m` with
`m = −0x1.72e0936a3d520p−33`.

**Adjudication.** Exact rational arithmetic shows `0x3FEFFFFFFFE8D1F7` (the
x86/AArch64/PowerPC/… result) is correctly rounded. The ARM soft-float result
`…F6` is off by more than half an ulp.

**Characterization.** In a 200 000-case fuzz of double addition, all 897
mismatches were effective subtractions with an **exponent gap of exactly
33**, and in every one the ARM result was the wrong one (rounded down).
ARM and Thumb-2, ARMv5 and ARMv7, Linux and bare-metal Cortex-M all agree on
the wrong answer, since they share libgcc's `ieee754-df.S`. Multiplication,
division and square root showed no mismatches.

**Mechanism (consistent with the source).** In `__adddf3`'s alignment step
for gaps ≥ 32, only one guard bit from the high word survives and the whole
low word is folded into a single sticky bit. When the subtraction then
cancels the leading bit and the result is renormalized left by one, the true
round bit was the low word's MSB, which has already been lost. That happens
only at gap 33, and it can only round down: the observed signature.

**Status.** Observed under QEMU with GCC 13's libgcc. The mechanism above is
read from the source and matches every observation, but confirming it on
silicon, and against QEMU's own ARM emulation as a factor, needs real hardware.
It has not yet been reported upstream.

**Why it matters.** `1.0 + small` with cancellation is everywhere in pinned
numerical kernels, and every FPU-less ARM MCU uses this routine (Cortex-M0,
M3, and M4F, whose FPU is single-precision only). Without the self-test, two
such satellites would agree with each other but **silently disagree with every
other platform.**

**Resolution.**
1. The self-test now includes six IEEE-754 rounding probes at exponent gaps
   31–34 and the full transcendental sweep fold. A platform with this defect
   is refused (`SAFE`).
2. `ports/c/softdouble.c` provides correctly-rounded `__aeabi_dadd`,
   `__aeabi_dsub` and the conversions that share libgcc's object, all 15
   symbols, so the archive member is never pulled in. Verified against
   x86-64 SSE2 hardware: **49.4 million add/sub cases** (every exponent gap
   0–66 against powers of two, ties, subnormals, ±0, ±∞, NaN, uniform random
   patterns) and **15 million conversions**, zero mismatches
   (`ports/c/tests/test_softdouble.c`). With it linked, Cortex-M3/M4F and
   ARMv5 are bit-identical to every other platform.

## Reproduce

```
apt install qemu-user qemu-system-arm gcc-arm-none-eabi libnewlib-arm-none-eabi \
    gcc-{i686,aarch64,powerpc,powerpc64le,riscv64,sparc64,mips,s390x}-linux-gnu \
    gcc-arm-linux-gnueabi{,hf}
tools/crossarch.sh --markdown
tools/baremetal.sh --markdown
```

Both run in CI (`.github/workflows/ci.yml`) on every push.

**What this does not show.** QEMU executes each instruction's architectural
semantics; it is not physical silicon (errata, a non-conformant FPU
microcode, radiation effects). The self-test is the bridge: it runs the same
known answers on the flight unit itself, at every boot.
