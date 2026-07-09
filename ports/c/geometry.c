// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// geometry.c — Vec3 arithmetic (pinned expansion order) and the linear
// encounter kernel screen(). Only +,-,*,/,sqrt on this path.

#include "starboard.h"
#include <math.h>

Vec3 sb_vadd(Vec3 a, Vec3 b)
{
    Vec3 r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    return r;
}

Vec3 sb_vsub(Vec3 a, Vec3 b)
{
    Vec3 r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}

Vec3 sb_vscale(Vec3 a, double s)
{
    Vec3 r;
    r.x = a.x * s;
    r.y = a.y * s;
    r.z = a.z * s;
    return r;
}

Vec3 sb_vdiv(Vec3 a, double s)
{
    Vec3 r;
    r.x = a.x / s;
    r.y = a.y / s;
    r.z = a.z / s;
    return r;
}

/* Pinned expansion order (SPEC §3.5): (x*ox + y*oy) + z*oz. */
double sb_vdot(Vec3 a, Vec3 b)
{
    return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

Vec3 sb_vcross(Vec3 a, Vec3 b)
{
    Vec3 r;
    r.x = (a.y * b.z) - (a.z * b.y);
    r.y = (a.z * b.x) - (a.x * b.z);
    r.z = (a.x * b.y) - (a.y * b.x);
    return r;
}

double sb_vnorm2(Vec3 a)
{
    return sb_vdot(a, a);
}

double sb_vnorm(Vec3 a)
{
    return sqrt(sb_vnorm2(a));
}

Vec3 sb_vnormalized(Vec3 a)
{
    const double n = sb_vnorm(a);
    Vec3 z;
    if (n > 0.0)
    {
        return sb_vdiv(a, n);
    }
    z.x = 0.0;
    z.y = 0.0;
    z.z = 0.0;
    return z;
}

Vec3 sb_vquantize(Vec3 v, double q)
{
    Vec3 r;
    r.x = sb_quantize(v.x, q);
    r.y = sb_quantize(v.y, q);
    r.z = sb_quantize(v.z, q);
    return r;
}

CraftState sb_canonicalize(CraftState s)
{
    CraftState c;
    c.position = sb_vquantize(s.position, SB_Q_POS_M);
    c.velocity = sb_vquantize(s.velocity, SB_Q_VEL_MPS);
    return c;
}

Conjunction sb_screen(CraftState a, CraftState b, double horizon_s,
                      double r_screen)
{
    const Vec3 dr = sb_vsub(b.position, a.position);
    const Vec3 dv = sb_vsub(b.velocity, a.velocity);
    const double dv2 = sb_vdot(dv, dv);
    Conjunction c;
    double t_raw;

    c.rel_velocity = dv;
    c.pc = 0.0;

    if (dv2 < 1.0e-12)
    {
        t_raw = 0.0;
    }
    else
    {
        t_raw = -(sb_vdot(dr, dv)) / dv2;
    }

    /* An out-of-window TCA (past, or beyond the horizon) means the pair is
     * assessed AT ITS CURRENT SEPARATION: a pair already inside the bubble
     * is detected regardless of where its linear TCA falls. */
    if ((t_raw <= 0.0) || (t_raw > horizon_s))
    {
        c.t_ca = 0.0;
        c.miss_vector = dr;
    }
    else
    {
        c.t_ca = t_raw;
        c.miss_vector = sb_vadd(dr, sb_vscale(dv, t_raw));
    }
    c.miss_distance = sb_vnorm(c.miss_vector);
    c.detected = (c.miss_distance < r_screen);
    return c;
}
