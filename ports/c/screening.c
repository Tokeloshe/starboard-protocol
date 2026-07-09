// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// screening.c — canonical conjunction search over a bounded J2 arc.
// Grid walk on the 32 s range-rate sign grid + fixed 48-step bisection.

#include "starboard.h"
#include <math.h>

static void sb_radial_bounds(CraftState s, double *r_lo, double *r_hi)
{
    const double rn = sb_vnorm(s.position);
    const double v2 = sb_vdot(s.velocity, s.velocity);
    const double energy = (0.5 * v2) - (SB_MU_EARTH_M3S2 / rn);
    double a;
    double h;
    double e2;
    double e;

    if (energy >= 0.0)
    {
        *r_lo = 0.0;
        *r_hi = 1.0e18;
        return;
    }
    a = -SB_MU_EARTH_M3S2 / (2.0 * energy);
    h = sb_vnorm(sb_vcross(s.position, s.velocity));
    e2 = 1.0 + (((2.0 * energy) * h * h) /
                (SB_MU_EARTH_M3S2 * SB_MU_EARTH_M3S2));
    if (e2 < 0.0)
    {
        e2 = 0.0;
    }
    e = sqrt(e2);
    *r_lo = a * (1.0 - e);
    *r_hi = a * (1.0 + e);
}

/* g(t) = dr.dv at time t; optionally returns dr. */
static double sb_g_at(CraftState a0, CraftState b0, double t, Vec3 *dr_out)
{
    const CraftState sa = sb_propagate(a0, t);
    const CraftState sb = sb_propagate(b0, t);
    const Vec3 dr = sb_vsub(sb.position, sa.position);
    const Vec3 dv = sb_vsub(sb.velocity, sa.velocity);
    if (dr_out != NULL)
    {
        *dr_out = dr;
    }
    return sb_vdot(dr, dv);
}

static bool sb_finalize(CraftState a0, CraftState b0, Sym33 cov_a, Sym33 cov_b,
                        double r_hb, double t_star, OrbitConjunction *oc)
{
    const double t_q = sb_quantize(t_star, SB_Q_TIME_S);
    Conjunction c;
    oc->a_tca = sb_canonicalize(sb_propagate(a0, t_q));
    oc->b_tca = sb_canonicalize(sb_propagate(b0, t_q));
    c.t_ca = t_q;
    c.miss_vector = sb_vsub(oc->b_tca.position, oc->a_tca.position);
    c.rel_velocity = sb_vsub(oc->b_tca.velocity, oc->a_tca.velocity);
    c.miss_distance = sb_vnorm(c.miss_vector);
    c.detected = (c.miss_distance < SB_R_SCREEN_M);
    c.pc = 0.0;
    if (c.detected)
    {
        c.pc = sb_starboard_pc(&c, sb_cov_quantize(sb_cov_add(cov_a, cov_b)),
                               r_hb);
    }
    oc->conj = c;
    return c.detected;
}

bool sb_screen_orbits(CraftState a0_in, CraftState b0_in,
                      Sym33 cov_a, Sym33 cov_b, double r_hb, double arc_s,
                      OrbitConjunction *out)
{
    const CraftState a0 = sb_canonicalize(a0_in);
    const CraftState b0 = sb_canonicalize(b0_in);
    double arc = arc_s;
    double alo;
    double ahi;
    double blo;
    double bhi;
    double gap;
    Vec3 dr0;
    double g0;
    double g_prev;
    double t_prev;
    double step;
    double t;

    if (arc > SB_SCREEN_ARC_MAX_S)
    {
        arc = SB_SCREEN_ARC_MAX_S;
    }
    if (arc <= 0.0)
    {
        return false;
    }

    sb_radial_bounds(a0, &alo, &ahi);
    sb_radial_bounds(b0, &blo, &bhi);
    gap = (alo > bhi) ? (alo - bhi) : (blo - ahi);
    if (gap > SB_R_SCREEN_M)
    {
        return false;
    }

    g0 = sb_g_at(a0, b0, 0.0, &dr0);
    if ((g0 >= 0.0) && (sb_vnorm(dr0) < SB_R_SCREEN_M))
    {
        if (sb_finalize(a0, b0, cov_a, cov_b, r_hb, 0.0, out))
        {
            return true;
        }
    }

    g_prev = g0;
    t_prev = 0.0;
    step = SB_SCREEN_STEP_S;
    for (t = step; ; t = ((t + step) < arc) ? (t + step) : arc)
    {
        const double g_now = sb_g_at(a0, b0, t, NULL);
        if ((g_prev < 0.0) && (g_now >= 0.0))
        {
            double lo = t_prev;
            double hi = t;
            int i;
            for (i = 0; i < SB_TCA_BISECT_ITERS; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                if (sb_g_at(a0, b0, mid, NULL) < 0.0)
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
            if (sb_finalize(a0, b0, cov_a, cov_b, r_hb,
                            0.5 * (lo + hi), out))
            {
                return true;
            }
        }
        g_prev = g_now;
        t_prev = t;
        if (t >= arc)
        {
            break;
        }
    }
    return false;
}
