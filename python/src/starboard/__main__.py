# SPDX-License-Identifier: AGPL-3.0-or-later
"""starboard — decide collision warnings from the command line.

    starboard warning.cdm --me 2026-001A --profile smallsat
    starboard warnings/*.cdm --me 58123 --json
    starboard --selftest
"""
import argparse
import json
import sys

import starboard


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="starboard",
        description="Read CCSDS conjunction warnings (CDMs) and print what "
                    "your satellite should do.")
    ap.add_argument("cdm", nargs="*", help="CDM file(s), KVN format")
    ap.add_argument("--me", help="your object: international designator, "
                                 "catalog number or name (default OBJECT1)")
    ap.add_argument("--profile", default="smallsat",
                    choices=starboard.PROFILES,
                    help="your spacecraft type (default smallsat)")
    ap.add_argument("--json", action="store_true",
                    help="one JSON object per warning")
    ap.add_argument("--selftest", action="store_true",
                    help="check this build computes bit-identically")
    ap.add_argument("--version", action="version",
                    version=f"starboard {starboard.__version__}")
    args = ap.parse_args(argv)

    bits = starboard.selftest()
    if bits != 0:
        print(f"self-test FAILED (bits {bits:#x}): this build must not be "
              "used", file=sys.stderr)
        return 3
    if args.selftest:
        print("self-test: all pass (bit-identical to every Starboard craft)")
        if not args.cdm:
            return 0
    if not args.cdm:
        ap.print_help()
        return 2

    errors = 0
    for path in args.cdm:
        a = starboard.decide(path, me=args.me, profile=args.profile)
        if a.action == "ERROR":
            errors += 1
        if args.json:
            d = a.to_dict()
            d["file"] = path
            print(json.dumps(d))
        else:
            if len(args.cdm) > 1:
                print(f"{path}:")
            print(a.summary)
            if a.action == "MOVE":
                print(f"  burn at {a.burn:%Y-%m-%d %H:%M:%S} UTC, "
                      f"dv RTN = [{a.dv_rtn_mps[0]:+.4f}, "
                      f"{a.dv_rtn_mps[1]:+.4f}, {a.dv_rtn_mps[2]:+.4f}] m/s, "
                      f"return {a.return_dv_mps:.4f} m/s")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
