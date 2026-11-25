# SPDX-License-Identifier: AGPL-3.0-or-later
"""Starboard: deterministic satellite collision avoidance.

Give it a collision warning (a CCSDS Conjunction Data Message, the file
space-tracking providers send) and say which object is yours::

    import starboard
    advice = starboard.decide("warning.cdm", me="2026-001A",
                              profile="smallsat")
    print(advice.summary)
    if advice.action == "MOVE":
        print(advice.burn, advice.dv_rtn_mps)

The other satellite, running Starboard on its own copy of the warning,
reaches the mirror-image answer: exactly one of you moves. The answer comes
from the same C flight code a spacecraft runs, so it is bit-identical.
"""
import ctypes as _ct
import dataclasses as _dc
import datetime as _dt
import os as _os
from typing import Optional, Tuple, Union

from ._core import PROFILES, _Advice, gps_ms_to_utc, lib as _lib, \
    utc_to_gps_ms

__version__ = "0.7.0"
__all__ = ["decide", "selftest", "Advice", "PROFILES", "ACTIONS",
           "gps_ms_to_utc", "utc_to_gps_ms"]

ACTIONS = {-1: "ERROR", 0: "NOTHING", 1: "MOVE", 2: "HOLD", 3: "ALERT"}


@_dc.dataclass(frozen=True)
class Advice:
    """What your satellite should do about one conjunction.

    action is one of:
      MOVE     you give way: perform the burn (burn, dv_rtn_mps)
      HOLD     the other craft gives way: keep your course steady
      NOTHING  low risk: carry on
      ALERT    you should give way but cannot maneuver: tell your operators
      ERROR    the warning could not be read (never treat this as safe)
    """
    action: str
    summary: str
    self_id: str
    other_id: str
    other_name: str
    conjunction_id: str
    miss_m: float
    pc: float
    tca: Optional[_dt.datetime]
    rule: int
    rule_text: str
    advisory: bool
    burn: Optional[_dt.datetime]
    dv_rtn_mps: Tuple[float, float, float]
    dv_mps: float
    return_dv_mps: float
    late: bool
    tca_gps_ms: int
    burn_gps_ms: int
    error: Optional[str]
    error_line: int

    def to_dict(self):
        d = _dc.asdict(self)
        for k in ("tca", "burn"):
            d[k] = d[k].isoformat() if d[k] is not None else None
        d["dv_rtn_mps"] = list(d["dv_rtn_mps"])
        return d


def _read(cdm):
    if isinstance(cdm, (bytes, bytearray)):
        return bytes(cdm)
    if isinstance(cdm, _os.PathLike) or (isinstance(cdm, str) and
                                         "\n" not in cdm and
                                         _os.path.isfile(cdm)):
        with open(cdm, "rb") as f:
            return f.read()
    if isinstance(cdm, str):
        return cdm.encode("utf-8")
    raise TypeError("cdm must be text, bytes or a file path")


def decide(cdm: Union[str, bytes, "_os.PathLike[str]"],
           me: Optional[str] = None, profile: str = "smallsat",
           now: Union[None, _dt.datetime, int] = None) -> Advice:
    """Decide one conjunction.

    cdm      CDM text, bytes, or a path to a .cdm file (KVN format)
    me       your object: international designator ("2026-001A"), catalog
             number ("58123") or OBJECT_NAME. None = OBJECT1.
    profile  one of PROFILES: no-propulsion, cubesat, smallsat,
             constellation, high-value, crewed
    now      current time (UTC datetime or GPS ms). None = 6 h before
             closest approach, convenient for analysis.
    """
    try:
        p = PROFILES.index(profile)
    except ValueError:
        raise ValueError(f"profile must be one of {PROFILES}") from None
    text = _read(cdm)
    if isinstance(now, _dt.datetime):
        now_ms = utc_to_gps_ms(now)
    else:
        now_ms = int(now or 0)
    a = _Advice()
    _lib.sb_easy_decide(text, len(text),
                        me.encode() if me is not None else None, p, now_ms,
                        _ct.byref(a))
    action = ACTIONS.get(a.action, "ERROR")
    ok = action != "ERROR"
    return Advice(
        action=action,
        summary=a.summary.decode(errors="replace"),
        self_id=a.self_id.decode(errors="replace"),
        other_id=a.other_id.decode(errors="replace"),
        other_name=a.other_name.decode(errors="replace"),
        conjunction_id=f"{a.conjunction_id:016X}",
        miss_m=a.miss_m,
        pc=a.pc,
        tca=gps_ms_to_utc(a.tca_gps_ms) if ok else None,
        rule=a.rule,
        rule_text=(a.rule_text or b"").decode(),
        advisory=bool(a.advisory),
        burn=gps_ms_to_utc(a.burn_gps_ms) if action == "MOVE" else None,
        dv_rtn_mps=tuple(a.dv_rtn_mps),
        dv_mps=a.dv_mps,
        return_dv_mps=a.return_dv_mps,
        late=bool(a.late),
        tca_gps_ms=a.tca_gps_ms,
        burn_gps_ms=a.burn_gps_ms,
        error=None if ok else (a.error_text or b"error").decode(),
        error_line=a.error_line,
    )


def selftest() -> int:
    """Run the built-in self-test. 0 = this build computes bit-identically
    to every other Starboard spacecraft; anything else: do not use it."""
    return int(_lib.sb_easy_selftest())
