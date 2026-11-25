# SPDX-License-Identifier: AGPL-3.0-or-later
# Run: python -m pytest python/tests   (or: python python/tests/test_starboard.py)
import datetime as dt
import json
import os
import subprocess
import sys

import starboard

EX = os.path.join(os.path.dirname(__file__), "..", "..", "examples", "cdm")


def cdm(name):
    return os.path.join(EX, name)


def test_selftest():
    assert starboard.selftest() == 0


def test_move_and_mirror():
    a = starboard.decide(cdm("shell_vs_debris.cdm"), me="SHELL-1042",
                         profile="constellation")
    assert a.action == "MOVE"
    assert a.burn is not None and a.burn < a.tca
    assert 0 < a.dv_mps < 2
    assert a.summary.startswith("MOVE: give way to 2026-911ABC")
    # Same warning, the debris' perspective: it cannot move -> ALERT is not
    # possible (it is debris) -> it is the stand-on craft or alerts.
    b = starboard.decide(cdm("shell_vs_debris.cdm"), me="DEB-FRAGMENT",
                         profile="no-propulsion")
    assert b.action in ("HOLD", "ALERT")
    assert a.conjunction_id == b.conjunction_id


def test_profiles_and_inputs():
    path = cdm("crossing_demo.cdm")
    text = open(path).read()
    for p in starboard.PROFILES:
        r1 = starboard.decide(path, me="ACME-1", profile=p)
        r2 = starboard.decide(text, me="ACME-1", profile=p)
        r3 = starboard.decide(text.encode(), me="99001", profile=p)
        assert r1 == r2 == r3, p
        assert r1.action in ("MOVE", "ALERT", "HOLD", "NOTHING")
    assert starboard.decide(path, me="ACME-1",
                            profile="no-propulsion").action == "ALERT"


def test_fail_safe():
    bad = starboard.decide("CCSDS_CDM_VERS = 1.0\nnonsense\n")
    assert bad.action == "ERROR" and bad.error
    assert "not as safe" in bad.summary
    nobody = starboard.decide(cdm("crossing_demo.cdm"), me="1999-999Z")
    assert nobody.action == "ERROR"


def test_time_round_trip():
    t = dt.datetime(2026, 11, 25, 12, 34, 56, 789000, tzinfo=dt.timezone.utc)
    assert starboard.gps_ms_to_utc(starboard.utc_to_gps_ms(t)) == t
    a = starboard.decide(cdm("crossing_demo.cdm"), me="ACME-1")
    assert starboard.utc_to_gps_ms(a.tca) == a.tca_gps_ms
    late = starboard.decide(cdm("crossing_demo.cdm"), me="ACME-1",
                            now=a.tca - dt.timedelta(minutes=30))
    assert late.action == "MOVE" and late.late


def test_cli_json():
    out = subprocess.run([sys.executable, "-m", "starboard",
                          cdm("shell_vs_debris.cdm"), "--me", "SHELL-1042",
                          "--profile", "constellation", "--json"],
                         capture_output=True, text=True, check=True).stdout
    d = json.loads(out)
    assert d["action"] == "MOVE" and len(d["dv_rtn_mps"]) == 3


if __name__ == "__main__":
    n = 0
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            n += 1
    print(f"python: {n} tests passed")
