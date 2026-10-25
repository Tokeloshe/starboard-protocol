# Evidence — fuzzing

Everything a spacecraft receives from outside — CDM text on the uplink,
maneuver notices on inter-satellite links — is attacker-reachable input.
`verification/fuzz/run.sh` runs four libFuzzer targets under
AddressSanitizer and UndefinedBehaviorSanitizer (`-fno-sanitize-recover`):
any crash, out-of-bounds access, leak, undefined behaviour, hang
(> 10 s per input) or *differential mismatch* fails the run.

| Target | What it attacks | Oracle |
|---|---|---|
| `fuzz_cdm_diff` | CDM binding, **C99 vs C++** on the same bytes, plus the screening step | identical status, identities, states, covariances, TCA, Pc bits |
| `fuzz_decimal_diff` | exact decimal ingest, C vs C++ | identical status and bits, **and** equal to glibc's correctly rounded `strtod` |
| `fuzz_fsw` | the whole flight layer fed hostile uplink | defined status, telemetry drains, nothing crashes |
| `fuzz_flock` | maneuver-notice decode + flock ledger + flock check | accepted notices re-encode to the same bytes; relay is 0 or 204 bytes |

## Results

5 minutes per target (20 min total), seeded with the example CDMs, a valid
notice and the regression inputs:

| Target | Executions | Result |
|---|---|---|
| `fuzz_decimal_diff` | 119 254 651 | clean |
| `fuzz_fsw` | 5 363 429 | clean |
| `fuzz_flock` | 2 967 369 | clean |
| `fuzz_cdm_diff` | 845 695 | clean (after the fix below) |

About **129 million executions**. CI runs every target for 60 s on every push.

## Bug found: identity truncation by an embedded NUL

The first 5-minute `fuzz_cdm_diff` run stopped on a disagreement. For a CDM
whose `INTERNATIONAL_DESIGNATOR` held a NUL byte (`2026-901A\0\x08`):

* the **C++** binding checked every byte of the field and rejected it
  (`ID`);
* the **C** binding validated the identity as a C string, stopped at the
  NUL, accepted `2026-901A`, and failed later on another field (`FRAME`).

That is not cosmetic. Two different byte strings mapped to the same
spacecraft identity, a truncation hole in exactly the field that decides
who is who. The C binding now validates by length, both implementations
reject the input with the same status, and the input is kept as a permanent
seed (`verification/fuzz/regressions/cdm_embedded_nul_id.bin`).

Two independent implementations fuzzed against each other found a defect
that neither implementation's own tests could see. Starboard's cross-language
discipline exists to catch exactly that.

## Also found during this work, by the other campaigns

| Defect | Found by | Effect | Fix |
|---|---|---|---|
| NaN screening arc loops forever | review, then probe | a hang: a watchdog reset in flight | `!(arc > 0)` guard (bit-neutral) |
| NaN state → "no conjunction" | probe | fail-open | input domain (SPEC §3.7), CDM rejection |
| NaN/Inf covariance → NaN Pc | probe | disables the probability gate | non-finite repair: Pc := 1 |
| raw-`memcmp` SEU comparison | fault-injection tests | false SEU alarms | canonical serialization |
| endian-dependent self-test fold | cross-arch campaign | big-endian craft refused to fly | explicit little-endian fold |
| ARM soft-float misrounding | cross-arch campaign | silent cross-platform disagreement | self-test probes + `softdouble.c` |
| `printf` argument order in a demo | the demo's own check | wrong digest printed | sequence the call first |
| `cdm.hpp` not self-contained | fuzz build | compile error in other contexts | includes fixed; every header now checked standalone |

## Reproduce

```
apt install clang libclang-rt-18-dev
verification/fuzz/run.sh 300        # seconds per target
```
