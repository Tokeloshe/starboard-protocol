// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// propagator.c — fixed-step RK4 over two-body + J2 zonal dynamics. Pinned
// stage order, pinned intra-vector expansion order, floor-via-(long)-cast step
// decomposition. Algebraic ops + sqrt only; no transcendental on this path.

#include "starboard.h"
#include <math.h>

Vec3 sb_accel_j2(Vec3 r)
{
    const double r2 = sb_vnorm2(r);
    const double rn = sqrt(r2);
    const double inv_r3 = 1.0 / (r2 * rn);
    Vec3 a = sb_vscale(r, (-SB_MU_EARTH_M3S2 * inv_r3));
    const double zr2 = (r.z * r.z) / r2;
    const double re2 = SB_R_EARTH_M * SB_R_EARTH_M;
    const double f = (-1.5 * SB_J2_EARTH * SB_MU_EARTH_M3S2 * re2) *
                     (inv_r3 / r2);
    const double c_xy = 1.0 - (5.0 * zr2);
    const double c_z = 3.0 - (5.0 * zr2);
    a.x += (f * r.x) * c_xy;
    a.y += (f * r.y) * c_xy;
    a.z += (f * r.z) * c_z;
    return a;
}

void sb_rk4_step(CraftState *s, double h)
{
    const Vec3 k1v = s->velocity;
    const Vec3 k1a = sb_accel_j2(s->position);
    const Vec3 k2v = sb_vadd(s->velocity, sb_vscale(k1a, 0.5 * h));
    const Vec3 k2a = sb_accel_j2(sb_vadd(s->position, sb_vscale(k1v, 0.5 * h)));
    const Vec3 k3v = sb_vadd(s->velocity, sb_vscale(k2a, 0.5 * h));
    const Vec3 k3a = sb_accel_j2(sb_vadd(s->position, sb_vscale(k2v, 0.5 * h)));
    const Vec3 k4v = sb_vadd(s->velocity, sb_vscale(k3a, h));
    const Vec3 k4a = sb_accel_j2(sb_vadd(s->position, sb_vscale(k3v, h)));
    const double h6 = h / 6.0;
    /* pos += ((k1v+k4v) + (k2v+k3v)*2) * h6 */
    s->position = sb_vadd(s->position,
        sb_vscale(sb_vadd(sb_vadd(k1v, k4v),
                          sb_vscale(sb_vadd(k2v, k3v), 2.0)), h6));
    s->velocity = sb_vadd(s->velocity,
        sb_vscale(sb_vadd(sb_vadd(k1a, k4a),
                          sb_vscale(sb_vadd(k2a, k3a), 2.0)), h6));
}

CraftState sb_propagate(CraftState s0, double dt)
{
    CraftState s = s0;
    double sign;
    double adt;
    double h;
    long n_full;
    long i;
    double rem;

    if (dt == 0.0)
    {
        return s;
    }
    sign = (dt < 0.0) ? -1.0 : 1.0;
    adt = sign * dt;
    h = sign * SB_PROP_STEP_S;
    n_full = (long)(adt / SB_PROP_STEP_S);
    for (i = 0; i < n_full; ++i)
    {
        sb_rk4_step(&s, h);
    }
    rem = adt - ((double)n_full * SB_PROP_STEP_S);
    if (rem > 0.0)
    {
        sb_rk4_step(&s, sign * rem);
    }
    return s;
}
