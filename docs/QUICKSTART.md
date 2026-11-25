# Starboard quickstart

*Five minutes from "what is this" to a working give-way decision, whichever
way you work.* Every path below runs the **same C flight code**, so the
answers are bit-identical across all of them.

You need one thing: a collision warning in the standard **CCSDS Conjunction
Data Message (CDM)** format, the file Space-Track, LeoLabs, Slingshot, ESA
and every other provider sends to operators. Three examples live in
[`examples/cdm/`](../examples/cdm/).

| Path | For | Install |
|---|---|---|
| [A. Browser](#a-browser-nothing-to-install) | anyone curious | nothing |
| [B. Python](#b-python) | ground segment, analysts, researchers | `pip install` |
| [C. Command line](#c-command-line) | operators, scripts, CI | `make` |
| [D. Drop into your C/C++ code](#d-drop-into-your-cc-code) | flight & ground software | copy 2 files |
| [E. Full flight integration](#e-full-flight-integration) | spacecraft teams | see [INTEGRATION.md](INTEGRATION.md) |

---

## Pick your profile

Starboard needs to know what kind of spacecraft you are. Pick the closest:

| Profile | Use for | Can maneuver | Priority | Burn window |
|---|---|---|---|---|
| `no-propulsion` | no thrusters, dead satellites, debris | no | operational | n/a (others give way; you get an alert) |
| `cubesat` | small thruster, commanded on ground passes | yes | operational | 1 h – 4.5 h ahead |
| `smallsat` | propulsive small/medium satellite | yes | operational | 30 min – 3 h ahead |
| `constellation` | autonomous constellation member (Starlink-class) | yes | operational | 20 min – 2 h 15 ahead |
| `high-value` | GNSS, weather, science flagships | yes | critical | 45 min – 4.5 h ahead |
| `crewed` | crewed vehicles and stations | yes | crewed (highest) | 45 min – 4.5 h ahead |

Starboard picks the **cheapest burn inside your window** (on a fixed
5-minute grid, so anyone holding the same warning and knowing your profile
computes the same burn). Burn cost is not monotonic in lead time: a burn
exactly one orbit ahead can cost 10× more than one half an orbit ahead, and
the search avoids that automatically.

## What you get back

| Action | Meaning | What to do |
|---|---|---|
| **MOVE** | you give way | perform the burn: time, Δv in RTN (radial, along-track, cross-track) |
| **HOLD** | the other craft gives way | keep your planned trajectory so it can predict you |
| **NOTHING** | low risk (Pc < 1e-4 and miss ≥ 1 km) | carry on |
| **ALERT** | you should give way but cannot maneuver | tell your operators now |
| **ERROR** | the warning could not be read | **never treat this as safe**: resolve it by hand |

Plus one plain-English sentence for your operators, for example:

> MOVE: give way to 2026-911ABC (DEB-FRAGMENT). Burn 0.277 m/s 2 h 15 min
> before closest approach (RTN +0.041, +0.272, -0.037 m/s). Closest approach
> 312 m, Pc 3.4e-3; decided by the capability rule (only one craft can
> maneuver).

---

## A. Browser, nothing to install

Open **<https://tokeloshe.github.io/starboard-protocol/try.html>**, click an
example or paste your CDM, pick each object's profile, press **Decide**. You
see what *both* satellites decide, side by side. The page runs the C flight
code compiled to WebAssembly and runs the self-test first, proving your
browser computes bit-identically to every Starboard satellite. Nothing is
uploaded.

## B. Python

```bash
pip install "git+https://github.com/Tokeloshe/starboard-protocol#subdirectory=python"
```

```python
import starboard

advice = starboard.decide("warning.cdm", me="2026-001A", profile="smallsat")
print(advice.summary)

if advice.action == "MOVE":
    print(advice.burn)          # datetime (UTC)
    print(advice.dv_rtn_mps)    # (radial, along-track, cross-track) m/s
```

`me` is your international designator, catalog number or `OBJECT_NAME`.
`decide()` also accepts CDM text or bytes, and `now=` (a UTC `datetime`) to
get a burn that respects how much time is actually left. `advice.to_dict()`
gives JSON-ready output. Building needs a C compiler (any `gcc`/`clang`, or
MSVC on Windows).

It installs a command too:

```bash
starboard warning.cdm --me 2026-001A --profile constellation
starboard incoming/*.cdm --me 58123 --json   # one JSON object per warning
starboard --selftest
```

## C. Command line

```bash
git clone https://github.com/Tokeloshe/starboard-protocol && cd starboard-protocol
make -C ports/c sbctl
ports/c/sbctl cdm examples/cdm/shell_vs_debris.cdm            # both perspectives
ports/c/sbctl cdm examples/cdm/shell_vs_debris.cdm --json     # for pipelines
ports/c/sbctl selftest
```

## D. Drop into your C/C++ code

Copy [`dist/starboard.c`](../dist/starboard.c) and
[`dist/starboard.h`](../dist/starboard.h) into your project (one file, like
SQLite). Compile with:

```bash
cc -std=c99 -O2 -ffp-contract=off -c starboard.c
```

`-ffp-contract=off` is **required**: it stops the compiler fusing a*b+c into
one rounding, which would change the bits. (If you forget, the self-test
catches it and refuses to run.) Then:

```c
#include "starboard.h"

if (sb_easy_selftest() != 0) { /* this build must not be used */ }

SbAdvice a;
sb_easy_decide(cdm_text, cdm_len, "2026-001A", SB_SAT_SMALLSAT, now_gps_ms, &a);
puts(a.summary);
if (a.action == SB_ACTION_MOVE) {
    schedule_burn(a.burn_gps_ms, a.dv_rtn_mps);   /* your AOCS */
}
```

No heap, no OS, no stdio in the library: it runs on a CubeSat
microcontroller as happily as on a server. Working example:
[`examples/quickstart.c`](../examples/quickstart.c).

```bash
cc -std=c99 -O2 -ffp-contract=off -Idist examples/quickstart.c dist/starboard.c -lm -o quickstart
./quickstart examples/cdm/shell_vs_debris.cdm SHELL-1042 constellation
```

## E. Full flight integration

The easy API answers one warning. A spacecraft also wants the encounter
table, radiation-tolerant redundant evaluation, CCSDS telemetry, and the
flock (telling neighbours about its maneuver). That is the flight layer,
`starboard_fsw.h`, and the decisions are identical. Start with
[`examples/flight_loop.c`](../examples/flight_loop.c), a complete
boot → warning → burn → notice → telemetry loop, then
[`INTEGRATION.md`](INTEGRATION.md) for NASA cFS, F Prime and RTOS mapping,
and [`FLIGHT.md`](FLIGHT.md) for every detail.

## Time

Starboard uses **GPS milliseconds** (since 1980-01-06, no leap seconds)
because spacecraft clocks do. GPS is currently 18 s ahead of UTC. The Python
package converts both ways (`starboard.gps_ms_to_utc`, `utc_to_gps_ms`); in C,
`sb_utc_to_gps_ms()` converts civil UTC.

## Without a registry

Out of the box each satellite trusts only itself and reads the other's
`MANEUVERABLE` field as an unverified claim. If both satellites then decide
to give way, Starboard's Theorem 2 guarantees their burns *add* separation,
so the worst case is spent fuel, never a collision. With the offline
registry ([REGISTRY.md](REGISTRY.md)) both share one verdict and exactly one
moves.
