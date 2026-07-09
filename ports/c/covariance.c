// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// covariance.c — Starboard-Pc via Patera's contour integral (N=32), pinned
// ascending summation. Uses +,-,*,/,sqrt plus canon_exp/canon_sincos only.

#include "starboard.h"
#include <math.h>

Sym33 sb_sym33_diagonal(double v)
{
    Sym33 r;
    r.xx = v; r.xy = 0.0; r.xz = 0.0;
    r.yy = v; r.yz = 0.0; r.zz = v;
    return r;
}

Sym33 sb_cov_add(Sym33 a, Sym33 b)
{
    Sym33 r;
    r.xx = a.xx + b.xx;
    r.xy = a.xy + b.xy;
    r.xz = a.xz + b.xz;
    r.yy = a.yy + b.yy;
    r.yz = a.yz + b.yz;
    r.zz = a.zz + b.zz;
    return r;
}

Sym33 sb_cov_quantize(Sym33 c)
{
    Sym33 r;
    r.xx = sb_quantize(c.xx, SB_Q_COV_M2);
    r.xy = sb_quantize(c.xy, SB_Q_COV_M2);
    r.xz = sb_quantize(c.xz, SB_Q_COV_M2);
    r.yy = sb_quantize(c.yy, SB_Q_COV_M2);
    r.yz = sb_quantize(c.yz, SB_Q_COV_M2);
    r.zz = sb_quantize(c.zz, SB_Q_COV_M2);
    return r;
}

double sb_cov_quad(Sym33 c, Vec3 u, Vec3 v)
{
    const double row_x = ((c.xx * v.x) + (c.xy * v.y)) + (c.xz * v.z);
    const double row_y = ((c.xy * v.x) + (c.yy * v.y)) + (c.yz * v.z);
    const double row_z = ((c.xz * v.x) + (c.yz * v.y)) + (c.zz * v.z);
    return ((u.x * row_x) + (u.y * row_y)) + (u.z * row_z);
}

static Vec3 sb_pinned_perpendicular(Vec3 n)
{
    const double ax = (n.x < 0.0) ? -n.x : n.x;
    const double ay = (n.y < 0.0) ? -n.y : n.y;
    const double az = (n.z < 0.0) ? -n.z : n.z;
    Vec3 axis;
    Vec3 p;
    axis.x = 1.0; axis.y = 0.0; axis.z = 0.0;
    if ((ay < ax) && (ay <= az))
    {
        axis.x = 0.0; axis.y = 1.0; axis.z = 0.0;
    }
    else if ((az < ax) && (az < ay))
    {
        axis.x = 0.0; axis.y = 0.0; axis.z = 1.0;
    }
    else
    {
        /* keep default axis */
    }
    p = sb_vsub(axis, sb_vscale(n, sb_vdot(axis, n)));
    return sb_vnormalized(p);
}

double sb_starboard_pc(const Conjunction *conj, Sym33 c_comb, double r_hb)
{
    const double vmag = sb_vnorm(conj->rel_velocity);
    Vec3 n;
    double d;
    Vec3 e1_raw;
    Vec3 e1_gs;
    double e1_gs_n;
    Vec3 e1;
    Vec3 e2;
    double d_p;
    double a;
    double b;
    double cx;
    double det;
    double det_floor;
    double inv;
    double L11;
    double L21;
    double L22;
    double R;
    double two_pi;
    double dphi;
    double sum;
    int i;
    double pc;

    if (vmag < 1.0e-9)
    {
        return (conj->miss_distance < r_hb) ? 1.0 : 0.0;
    }
    n = sb_vdiv(conj->rel_velocity, vmag);
    d = conj->miss_distance;
    if (d > SB_Q_POS_M)
    {
        e1_raw = sb_vdiv(conj->miss_vector, d);
    }
    else
    {
        e1_raw = sb_pinned_perpendicular(n);
    }
    e1_gs = sb_vsub(e1_raw, sb_vscale(n, sb_vdot(e1_raw, n)));
    e1_gs_n = sb_vnorm(e1_gs);
    if (e1_gs_n > 1.0e-9)
    {
        e1 = sb_vdiv(e1_gs, e1_gs_n);
    }
    else
    {
        e1 = sb_pinned_perpendicular(n);
    }
    e2 = sb_vcross(n, e1);
    d_p = sb_vdot(conj->miss_vector, e1);

    a  = sb_cov_quad(c_comb, e1, e1);
    b  = sb_cov_quad(c_comb, e2, e2);
    cx = sb_cov_quad(c_comb, e1, e2);
    if (a < SB_VAR_FLOOR_M2)
    {
        a = SB_VAR_FLOOR_M2;
    }
    if (b < SB_VAR_FLOOR_M2)
    {
        b = SB_VAR_FLOOR_M2;
    }
    det = (a * b) - (cx * cx);
    det_floor = SB_VAR_FLOOR_M2 * SB_VAR_FLOOR_M2;
    if (det < det_floor)
    {
        det = det_floor;
    }

    inv = 1.0 / det;
    L11 = sqrt(b * inv);
    L21 = (-cx * inv) / L11;
    L22 = sqrt((a * inv) - (L21 * L21));

    R = r_hb;
    two_pi = 2.0 * SB_PI;
    dphi = two_pi / (double)SB_PATERA_NODES;
    sum = 0.0;
    for (i = 0; i < SB_PATERA_NODES; ++i)
    {
        double cphi;
        double sphi;
        double sx;
        double sy;
        double dsx;
        double dsy;
        double ux;
        double uy;
        double dux;
        double duy;
        double w;
        double cross;
        double factor;
        sb_canon_sincos(dphi * (double)i, &sphi, &cphi);
        sx = -d_p + (R * cphi);
        sy = R * sphi;
        dsx = -R * sphi;
        dsy = R * cphi;
        ux = (L11 * sx) + (L21 * sy);
        uy = L22 * sy;
        dux = (L11 * dsx) + (L21 * dsy);
        duy = L22 * dsy;
        w = (ux * ux) + (uy * uy);
        cross = (ux * duy) - (uy * dux);
        if (w < SB_Q_COV_M2)
        {
            factor = 0.5 - (0.125 * w);
        }
        else
        {
            factor = (1.0 - sb_canon_exp(-0.5 * w)) / w;
        }
        sum += factor * cross;
    }
    pc = sum / (double)SB_PATERA_NODES;
    if (pc < 0.0)
    {
        pc = 0.0;
    }
    if (pc > 1.0)
    {
        pc = 1.0;
    }
    return pc;
}

bool sb_actionable(const Conjunction *conj)
{
    return conj->detected &&
           ((conj->pc >= SB_PC_ACTIONABLE) ||
            (conj->miss_distance < SB_R_SAFE_M));
}

Conjunction sb_assess(CraftState a, CraftState b, Sym33 cov_a, Sym33 cov_b,
                      double r_hb, double horizon_s)
{
    Conjunction c = sb_screen(sb_canonicalize(a), sb_canonicalize(b),
                              horizon_s, SB_R_SCREEN_M);
    if (c.detected)
    {
        c.pc = sb_starboard_pc(&c, sb_cov_quantize(sb_cov_add(cov_a, cov_b)),
                               r_hb);
    }
    return c;
}
