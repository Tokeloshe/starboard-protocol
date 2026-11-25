# starboard (Python)

**Deterministic satellite collision avoidance in one call.** Give it a
collision warning (a CCSDS Conjunction Data Message, the file every
space-tracking provider sends) and it tells your satellite what to do:
move, hold course, nothing, or alert the operators. The other satellite,
running Starboard on its own copy of the warning, reaches the mirror-image
answer, so exactly one of you moves.

```bash
pip install "git+https://github.com/Tokeloshe/starboard-protocol#subdirectory=python"
```

```python
import starboard

advice = starboard.decide("warning.cdm", me="2026-001A", profile="smallsat")
print(advice.summary)
# MOVE: give way to 2026-911ABC (DEB-FRAGMENT). Burn 0.277 m/s 2 h 15 min before
# closest approach (RTN +0.041, +0.272, -0.037 m/s). Closest approach 312 m, ...

if advice.action == "MOVE":
    advice.burn          # datetime, UTC
    advice.dv_rtn_mps    # (radial, along-track, cross-track) m/s
```

| `profile` | for |
|---|---|
| `no-propulsion` | no thrusters, dead satellites, debris |
| `cubesat` | small thruster, ground-commanded |
| `smallsat` | propulsive small/medium satellite |
| `constellation` | autonomous constellation member |
| `high-value` | GNSS, weather, science flagships |
| `crewed` | crewed vehicles and stations |

`advice.action` is one of `MOVE`, `HOLD`, `NOTHING`, `ALERT` (you should
give way but cannot maneuver) or `ERROR` (the warning could not be read;
never treat it as safe). `advice.to_dict()` is JSON-ready.

Command line:

```bash
starboard warning.cdm --me 2026-001A --profile constellation
starboard incoming/*.cdm --me 58123 --json
starboard --selftest
```

The package wraps the C flight library itself (the code a spacecraft
runs), so Python gets the same bits a satellite computes. Building it needs
a C compiler. Documentation:
[quickstart](https://github.com/Tokeloshe/starboard-protocol/blob/main/docs/QUICKSTART.md) ·
[project](https://github.com/Tokeloshe/starboard-protocol).
License: AGPL-3.0-or-later (commercial license available).
