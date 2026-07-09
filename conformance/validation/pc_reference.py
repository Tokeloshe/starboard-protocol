# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — independent Pc reference & validation.
# Copyright (C) 2026 James H. All rights reserved.
#
# Validates Starboard-Pc (Chan's series, fixed order) against an INDEPENDENT
# implementation: the encounter-plane projection is re-derived from the SPEC
# (DESIGN.md §4.4), and the collision integral is evaluated by adaptive 2-D
# quadrature (scipy) over the hard-body disc — a different algorithm from the
# series. Agreement therefore validates both the projection and the series.
#
#   g++ -std=c++17 -ffp-contract=off -O2 -Icore/include \
#       conformance/validation/pc_dump.cpp -o /tmp/pc_dump && /tmp/pc_dump > /tmp/pc.csv
#   python3 conformance/validation/pc_reference.py /tmp/pc.csv
#
# Exit status is nonzero if any case exceeds the documented error bound.

import sys
import numpy as np
from scipy import integrate


def pinned_perpendicular(n):
    ax, ay, az = abs(n[0]), abs(n[1]), abs(n[2])
    axis = np.array([1.0, 0.0, 0.0])
    if ay < ax and ay <= az:
        axis = np.array([0.0, 1.0, 0.0])
    elif az < ax and az < ay:
        axis = np.array([0.0, 0.0, 1.0])
    p = axis - n * axis.dot(n)
    return p / np.linalg.norm(p)


def true_pc(miss, vrel, C, R):
    """Reference Pc: project per SPEC §4.4, integrate the disc (adaptive)."""
    vmag = np.linalg.norm(vrel)
    if vmag < 1e-9:
        return 1.0 if np.linalg.norm(miss) < R else 0.0
    n = vrel / vmag
    d = np.linalg.norm(miss)
    e1r = (miss / d) if d > 1e-3 else pinned_perpendicular(n)
    e1g = e1r - n * e1r.dot(n)
    ng = np.linalg.norm(e1g)
    e1 = (e1g / ng) if ng > 1e-9 else pinned_perpendicular(n)
    e2 = np.cross(n, e1)
    a = e1 @ C @ e1
    b = e2 @ C @ e2
    c = e1 @ C @ e2
    dp = miss.dot(e1)
    # 2-D encounter-plane covariance and its inverse.
    S = np.array([[a, c], [c, b]])
    det = np.linalg.det(S)
    Sinv = np.linalg.inv(S)
    norm = 1.0 / (2.0 * np.pi * np.sqrt(det))
    mean = np.array([dp, 0.0])

    def integrand(y, x):  # dblquad calls f(y, x)
        u = np.array([x, y]) - mean
        return norm * np.exp(-0.5 * u @ Sinv @ u)

    # Integrate over the disc x^2 + y^2 <= R^2.
    val, _ = integrate.dblquad(
        integrand, -R, R,
        lambda x: -np.sqrt(max(R * R - x * x, 0.0)),
        lambda x: np.sqrt(max(R * R - x * x, 0.0)),
        epsabs=1e-14, epsrel=1e-11)
    return val


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pc.csv"
    rows = np.genfromtxt(path, delimiter=",", names=True)
    BOUND = 1e-4          # required relative accuracy of the Pc method
    FLOOR = 1e-12         # ignore Pc below the numerically-meaningful floor
    worst = 0.0
    worst_desc = ""
    n_checked = 0
    n_fail = 0
    print(f"{'miss(m)':>9} {'hbr':>5} {'Pc(series)':>12} "
          f"{'Pc(ref)':>12} {'rel.err':>10}")
    for r in np.atleast_1d(rows):
        miss = np.array([r['mx'], r['my'], r['mz']])
        vrel = np.array([r['vx'], r['vy'], r['vz']])
        C = np.array([[r['cxx'], r['cxy'], r['cxz']],
                      [r['cxy'], r['cyy'], r['cyz']],
                      [r['cxz'], r['cyz'], r['czz']]])
        R = float(r['hbr'])
        pc_series = float(r['pc'])
        pc_ref = true_pc(miss, vrel, C, R)
        if pc_ref < FLOOR and pc_series < FLOOR:
            continue
        n_checked += 1
        denom = max(pc_ref, FLOOR)
        rel = abs(pc_series - pc_ref) / denom
        if rel > worst:
            worst = rel
            worst_desc = (f"miss={np.linalg.norm(miss):.1f} R={R} "
                          f"series={pc_series:.4e} ref={pc_ref:.4e}")
        if rel > BOUND:
            n_fail += 1
            flag = "  <-- EXCEEDS"
        else:
            flag = ""
        # Print a readable sample (every case with meaningful Pc).
        if pc_ref > 1e-6 or rel > BOUND:
            print(f"{np.linalg.norm(miss):9.1f} {R:5.0f} {pc_series:12.4e} "
                  f"{pc_ref:12.4e} {rel:10.2e}{flag}")
    print(f"\nchecked {n_checked} cases with meaningful Pc; "
          f"max relative error = {worst:.3e}")
    print(f"worst case: {worst_desc}")
    print(f"error bound = {BOUND:.0e}")
    if n_fail == 0:
        print("PASS: Starboard-Pc matches the independent reference within "
              "the bound.")
        return 0
    print(f"FAIL: {n_fail} cases exceeded the bound.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
