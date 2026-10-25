# Evidence — MISRA C:2012 compliance record

`tools/misra.sh` runs cppcheck's MISRA C:2012 addon, plus cppcheck's general
defect checks, over all 17 flight-library sources (`ports/c`). The script
**fails** if a finding appears under a rule not listed below, or if cppcheck
reports a non-style defect. Each remaining rule is a documented deviation
with a rationale, as MISRA's own compliance process requires. Rules that
were cheap to satisfy were fixed rather than deviated (see the end).

This is a tool-assisted record, not a certified MISRA audit: cppcheck's
addon checks a subset of the guidelines, and a flight program should add its
own qualified checker and review.

## Deviations

| Rule | Category | Where | Rationale |
|---|---|---|---|
| 15.5 | Advisory | everywhere | *Single point of exit.* The code uses early returns for guard clauses and fail-safe rejections ("reject, report, return"). Rewriting each into nested flags hides the fail-safe path, which is the path a reviewer most needs to see. The most-deviated advisory rule in industry for this reason. |
| 8.7 | Advisory | public API | Functions with external linkage referenced in one translation unit: this is a *library*; they are its API (`starboard*.h`) and must stay external. |
| 10.1, 10.4, 10.6, 10.7, 10.8 | Required | arithmetic kernels | Essential-type model. The verdict path's operation order is **normative** (SPEC §3.5); inserting casts to satisfy the essential-type model risks changing evaluation order or precision, and every flagged expression is covered bit-for-bit by the v1/v2 transcripts on 11 architectures. Integer cases (SHA-512, Ed25519, CRC) are fixed-width unsigned arithmetic verified against FIPS/RFC vectors and fuzzing. |
| 14.1 | Required | `flock.c` check loop, `screening.c` grid walk | Floating loop counter. The screening grid walk is normative (SPEC §4.2a) and pinned bit-for-bit. Both loops are bounded by explicit caps (`SCREEN_ARC_MAX_S`, `SB_FLOCK_MAX_WINDOW_S`) and a NaN-proof guard, as the hang fix of SPEC §3.7 shows. |
| 14.2 | Required | `screening.c`, `registry.c` | `for` loops whose control variable is updated in the clause by a clamped expression (`t = min(t + step, arc)`). Pinned normative code. |
| 15.4 | Advisory | `broadphase.c`, `cdm.c` | More than one `break` in a loop: the heapsort sift-down and the line scanner. Bounded, single-level loops. |
| 17.8 | Advisory | `registry.c`, `softdouble.c` | Modifying a function parameter (the Merkle level count, a working mantissa). Local by-value copies; no caller-visible effect. |
| 18.4 | Advisory | `sha512.c` | Pointer arithmetic on byte buffers in the hash's block loop. Bounds are proven by the length arithmetic and covered by fuzzing under ASan. |
| 21.15 | Required | `fsw.c`, `flock.c` | `memcpy` between `double` and `uint64_t`: the only strictly portable way to read IEEE-754 bit patterns, which the whole protocol is defined over. |
| 11.5 | Advisory | `fsw.c` | `const void *` → `const uint8_t *` in the byte serializer. |
| 12.2, 12.3 | Required | `fsw.c`, `starboard.h` | Shift ranges on constant-width fields (checked); `12.3` is reported on the C11-compatible compile-time assertion `typedef char sb_assert_double_is_8_bytes[...]`. |
| 8.9 | Advisory | constant tables | File-scope `static const` tables used by one function (KAT data, power-of-ten table). Kept at file scope so the known answers sit together and are reviewable. |
| 9.3 | Required | `maneuver.c` | Reported on struct initialization of the CW matrices, which initialize every element; the finding is the tool's view of the `Mat3` wrapper. |
| 8.4 | Required | `starboard_registry.h` (`sb_merkle_proof`) | Tool limitation: the prototype and the definition are identical; cppcheck 2.13 mis-parses the pointer-to-array parameter `uint8_t (*nodes)[64]`. |
| 2.3, 2.5 | Advisory | headers | The compile-time `sizeof(double) == 8` assertion type; enum types used for their constants; configuration macros intentionally provided for integrators. |

## Fixed in this work rather than deviated

* **7.3**: every lowercase integer suffix (`ull`, `ll`, `u`) uppercased
  across the port, bit-neutral (transcripts unchanged).
* **8.4**: diagnostic entry points (`sb_fsw_kat_cdm_fold`, …) declared in
  `starboard_fsw.h`; test-only fault-injection globals declared `extern`.
* **12.1**: explicit parentheses in the epoch arithmetic.
* **17.8**: `days_from_civil` no longer modifies its parameter.
* **21.14**: no `memcmp` against string literals; length-checked compare.

## Reproduce

```
apt install cppcheck
tools/misra.sh            # summary; fails on an undeviated rule
tools/misra.sh --full     # every finding with file:line
```
