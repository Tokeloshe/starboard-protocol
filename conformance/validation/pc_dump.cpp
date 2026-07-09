// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — Pc validation harness (dumper).
// Copyright (C) 2026 James H. All rights reserved.
//
// Emits CSV rows of (miss vector, relative velocity, combined covariance,
// hard-body radius, Starboard-Pc) over a parameter grid. pc_reference.py
// reads this, re-projects per the SPEC and integrates the encounter-plane
// Gaussian over the hard-body disc with adaptive quadrature (an independent
// implementation AND an independent integration method), and reports the
// relative error of Starboard-Pc (the Patera contour integral) against that
// reference.
//
//   g++ -std=c++17 -ffp-contract=off -O2 -Icore/include
//       conformance/validation/pc_dump.cpp -o /tmp/pc_dump && /tmp/pc_dump

#include "starboard/covariance.hpp"

#include <cstdio>
#include <initializer_list>

using namespace starboard;

static void emit(const Vec3& miss, const Vec3& vrel, const Sym33& c_in,
                 double hbr) {
    // Quantize the covariance exactly as the flight path does (assess()
    // combines then quantizes), so the reference validates the arithmetic
    // the verdict actually consumes — and the CSV records the same matrix.
    const Sym33 c = cov_quantize(c_in);
    Conjunction conj;
    conj.detected = true;
    conj.t_ca = 100.0;
    conj.miss_vector = miss;
    conj.miss_distance = miss.norm();
    conj.rel_velocity = vrel;
    conj.pc = 0.0;
    const double pc = starboard_pc(conj, c, hbr);
    std::printf("%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,"
                "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.12e\n",
                miss.x, miss.y, miss.z, vrel.x, vrel.y, vrel.z,
                c.xx, c.xy, c.xz, c.yy, c.yz, c.zz, hbr, pc);
}

int main() {
    std::printf("mx,my,mz,vx,vy,vz,cxx,cxy,cxz,cyy,cyz,czz,hbr,pc\n");
    const Vec3 vrel(7000.0, 0.0, 0.0);          // encounter plane = y-z
    const Vec3 vrel_skew(6000.0, 2000.0, 500.0);// exercise the projection

    // Isotropic covariance, sweep miss and sigma and hbr (miss along z,
    // perpendicular to vrel).
    for (int mi = 0; mi < 12; ++mi) {
        const double miss = 10.0 * static_cast<double>(1 << mi); // 10..20480 m
        for (double sig : {50.0, 150.0, 500.0, 1000.0}) {
            for (double hbr : {5.0, 20.0, 50.0}) {
                emit(Vec3(0.0, 0.0, miss), vrel, Sym33::diagonal(sig * sig),
                     hbr);
            }
        }
    }
    // Anisotropic + correlated covariance (full 3x3), perpendicular miss.
    for (double miss : {50.0, 200.0, 800.0}) {
        for (double sy : {80.0, 400.0}) {
            for (double sz : {120.0, 600.0}) {
                Sym33 c{ (sy * sy), 0.0, 0.0, (sy * sy), 0.0, (sz * sz) };
                // add cross terms (correlation) in the y-z plane
                c.yz = 0.4 * sy * sz;
                emit(Vec3(0.0, 0.0, miss), vrel, c, 20.0);
                emit(Vec3(0.0, miss * 0.6, miss * 0.8), vrel, c, 15.0);
            }
        }
    }
    // Skew relative velocity so the encounter-plane basis is non-trivial.
    for (double miss : {40.0, 300.0, 1200.0}) {
        Sym33 c{ 4.0e4, 1.0e4, -0.5e4, 9.0e4, 2.0e4, 2.5e4 };
        emit(Vec3(0.0, miss, 0.0), vrel_skew, c, 25.0);
        emit(Vec3(miss * 0.3, -miss * 0.5, miss * 0.8), vrel_skew, c, 10.0);
    }
    return 0;
}
