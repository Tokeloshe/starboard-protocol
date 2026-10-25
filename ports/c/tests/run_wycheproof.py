#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — run Project Wycheproof's Ed25519 vectors against the
# C99 port and the C++ header. Vectors: vectors/ed25519_test.json
# (C2SP/wycheproof, Apache-2.0; see vectors/README.md).
#
# Starboard is STRICTER than RFC 8032 in one documented way: it rejects
# small-order public keys (REGISTRY.md §8, repudiation defence). A vector
# Wycheproof marks "valid" that Starboard rejects is acceptable ONLY if its
# public key is small-order; anything else fails this script.
import json
import subprocess
import sys

SMALL_ORDER = {  # the 8 canonical small-order encodings (+ non-canonical ones)
    "0100000000000000000000000000000000000000000000000000000000000000",
    "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000080",
    "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
    "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
    "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
    "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
}


def main():
    vec, drv = sys.argv[1], sys.argv[2]
    d = json.load(open(vec))
    cases = []
    for g in d["testGroups"]:
        pk = g["publicKey"]["pk"]
        for t in g["tests"]:
            cases.append((t["tcId"], pk, t["msg"] or "-", t["sig"], t["result"],
                          t.get("comment", "")))
    inp = "".join(f"{pk} {m} {s if len(s) else '00'}\n" for _, pk, m, s, _, _ in cases)
    out = subprocess.run([drv], input=inp, capture_output=True, text=True,
                         check=True).stdout.split("\n")
    agree = mismatch_impl = strict = bad = 0
    for (tc, pk, _, _, res, cm), line in zip(cases, out):
        c, p = (int(x) for x in line.split())
        if c != p:
            mismatch_impl += 1
            print(f"  tc {tc}: C={c} C++={p}  <-- C/C++ DISAGREE")
            continue
        want = 1 if res == "valid" else 0 if res == "invalid" else None
        if want is None or c == want:
            agree += 1
        elif want == 1 and c == 0 and pk.lower() in SMALL_ORDER:
            strict += 1
        else:
            bad += 1
            print(f"  tc {tc}: got {c}, Wycheproof expects {res} ({cm})")
    print(f"wycheproof ed25519: {len(cases)} vectors; {agree} agree with the "
          f"expected result; {strict} stricter by design (small-order key); "
          f"{bad} wrong; {mismatch_impl} C/C++ disagreements")
    return 0 if (bad == 0 and mismatch_impl == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
