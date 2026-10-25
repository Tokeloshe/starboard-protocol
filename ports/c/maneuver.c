// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// maneuver.c — Hill (RIC) frame, Clohessy-Wiltshire state transition, the
// minimum-dv give-way burn and the mirror+phase / quarter-period returns.
// Operation-for-operation transliteration of frames.hpp + maneuver.hpp (same
// expansion order, same canon_sincos), so the C99 port reproduces the C++
// maneuver plans bit-for-bit (conformance/expected_bits_ext.txt). Maneuver
// quantities are OFF the normative verdict path (SPEC §3.5); bit-equality
// here is a cross-language cross-check, not a protocol requirement.

#include "starboard.h"
#include <math.h>

Mat3 sb_mat3_identity(void)
{
    Mat3 m;
    int i;
    int j;
    for (i = 0; i < 3; ++i)
    {
        for (j = 0; j < 3; ++j)
        {
            m.m[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }
    return m;
}

Vec3 sb_mat3_mul(const Mat3 *a, Vec3 v)
{
    Vec3 r;
    r.x = ((a->m[0][0] * v.x) + (a->m[0][1] * v.y)) + (a->m[0][2] * v.z);
    r.y = ((a->m[1][0] * v.x) + (a->m[1][1] * v.y)) + (a->m[1][2] * v.z);
    r.z = ((a->m[2][0] * v.x) + (a->m[2][1] * v.y)) + (a->m[2][2] * v.z);
    return r;
}

Vec3 sb_mat3_tmul(const Mat3 *a, Vec3 v)
{
    Vec3 r;
    r.x = ((a->m[0][0] * v.x) + (a->m[1][0] * v.y)) + (a->m[2][0] * v.z);
    r.y = ((a->m[0][1] * v.x) + (a->m[1][1] * v.y)) + (a->m[2][1] * v.z);
    r.z = ((a->m[0][2] * v.x) + (a->m[1][2] * v.y)) + (a->m[2][2] * v.z);
    return r;
}

Vec3 sb_mat3_solve(const Mat3 *a, Vec3 b)
{
    const double c00 = (a->m[1][1] * a->m[2][2]) - (a->m[1][2] * a->m[2][1]);
    const double c01 = (a->m[1][2] * a->m[2][0]) - (a->m[1][0] * a->m[2][2]);
    const double c02 = (a->m[1][0] * a->m[2][1]) - (a->m[1][1] * a->m[2][0]);
    const double det = ((a->m[0][0] * c00) + (a->m[0][1] * c01)) +
                       (a->m[0][2] * c02);
    const double inv = 1.0 / det;
    const double c10 = (a->m[0][2] * a->m[2][1]) - (a->m[0][1] * a->m[2][2]);
    const double c11 = (a->m[0][0] * a->m[2][2]) - (a->m[0][2] * a->m[2][0]);
    const double c12 = (a->m[0][1] * a->m[2][0]) - (a->m[0][0] * a->m[2][1]);
    const double c20 = (a->m[0][1] * a->m[1][2]) - (a->m[0][2] * a->m[1][1]);
    const double c21 = (a->m[0][2] * a->m[1][0]) - (a->m[0][0] * a->m[1][2]);
    const double c22 = (a->m[0][0] * a->m[1][1]) - (a->m[0][1] * a->m[1][0]);
    Vec3 r;
    r.x = (((c00 * b.x) + (c10 * b.y)) + (c20 * b.z)) * inv;
    r.y = (((c01 * b.x) + (c11 * b.y)) + (c21 * b.z)) * inv;
    r.z = (((c02 * b.x) + (c12 * b.y)) + (c22 * b.z)) * inv;
    return r;
}

Mat3 sb_hill_basis(CraftState s)
{
    const Vec3 rhat = sb_vnormalized(s.position);
    const Vec3 chat = sb_vnormalized(sb_vcross(s.position, s.velocity));
    const Vec3 ihat = sb_vcross(chat, rhat);
    Mat3 m;
    m.m[0][0] = rhat.x; m.m[0][1] = rhat.y; m.m[0][2] = rhat.z;
    m.m[1][0] = ihat.x; m.m[1][1] = ihat.y; m.m[1][2] = ihat.z;
    m.m[2][0] = chat.x; m.m[2][1] = chat.y; m.m[2][2] = chat.z;
    return m;
}

double sb_mean_motion(CraftState s)
{
    const double a = sb_quantize(sb_vnorm(s.position), SB_Q_POS_M);
    return sqrt(SB_MU_EARTH_M3S2 / ((a * a) * a));
}

static Mat3 sb_mat3_zero(void)
{
    Mat3 m;
    int i;
    int j;
    for (i = 0; i < 3; ++i)
    {
        for (j = 0; j < 3; ++j)
        {
            m.m[i][j] = 0.0;
        }
    }
    return m;
}

Mat3 sb_cw_phi_rr(double n, double t)
{
    double s;
    double c;
    Mat3 m = sb_mat3_zero();
    sb_canon_sincos(n * t, &s, &c);
    m.m[0][0] = 4.0 - (3.0 * c);
    m.m[1][0] = 6.0 * (s - (n * t));
    m.m[1][1] = 1.0;
    m.m[2][2] = c;
    return m;
}

Mat3 sb_cw_phi_rv(double n, double t)
{
    double s;
    double c;
    Mat3 m = sb_mat3_zero();
    sb_canon_sincos(n * t, &s, &c);
    m.m[0][0] = s / n;
    m.m[0][1] = (2.0 * (1.0 - c)) / n;
    m.m[1][0] = (-2.0 * (1.0 - c)) / n;
    m.m[1][1] = ((4.0 * s) - ((3.0 * n) * t)) / n;
    m.m[2][2] = s / n;
    return m;
}

Mat3 sb_cw_phi_vr(double n, double t)
{
    double s;
    double c;
    Mat3 m = sb_mat3_zero();
    sb_canon_sincos(n * t, &s, &c);
    m.m[0][0] = (3.0 * n) * s;
    m.m[1][0] = (-6.0 * n) * (1.0 - c);
    m.m[2][2] = (-n) * s;
    return m;
}

Mat3 sb_cw_phi_vv(double n, double t)
{
    double s;
    double c;
    Mat3 m = sb_mat3_zero();
    sb_canon_sincos(n * t, &s, &c);
    m.m[0][0] = c;
    m.m[0][1] = 2.0 * s;
    m.m[1][0] = -2.0 * s;
    m.m[1][1] = (4.0 * c) - 3.0;
    m.m[2][2] = c;
    return m;
}

HillState sb_cw_propagate(HillState x0, double n, double t)
{
    const Mat3 rr = sb_cw_phi_rr(n, t);
    const Mat3 rv = sb_cw_phi_rv(n, t);
    const Mat3 vr = sb_cw_phi_vr(n, t);
    const Mat3 vv = sb_cw_phi_vv(n, t);
    HillState x;
    x.r = sb_vadd(sb_mat3_mul(&rr, x0.r), sb_mat3_mul(&rv, x0.v));
    x.v = sb_vadd(sb_mat3_mul(&vr, x0.r), sb_mat3_mul(&vv, x0.v));
    return x;
}

/* SPEC §6.4 — see maneuver.hpp for the derivation and the Theorem 2
 * argument for the degenerate zero-miss push direction. */
GiveWayPlan sb_plan_give_way(CraftState self_now, CraftState self_tca,
                             const Conjunction *conj,
                             bool self_is_canonical_lo, double r_safe)
{
    GiveWayPlan plan;
    const double target = SB_MANEUVER_MARGIN * r_safe;
    const bool pc_hot = (conj->pc >= SB_PC_ACTIONABLE);
    double n;
    double tau;
    Vec3 u_away_eci;
    double d_needed;
    Vec3 u_hill;
    Mat3 basis_tca;
    Mat3 basis_now;
    Mat3 phi;
    Vec3 w;
    double w2;

    if ((conj->miss_distance >= target) && (!pc_hot))
    {
        plan.required = false;
        plan.dv_eci.x = 0.0;
        plan.dv_eci.y = 0.0;
        plan.dv_eci.z = 0.0;
        plan.dv_hill = plan.dv_eci;
        plan.dv_mag = 0.0;
        plan.displacement_m = 0.0;
        return plan;
    }
    plan.required = true;

    n = sb_mean_motion(self_now);
    tau = (conj->t_ca > SB_MIN_LEAD_S) ? conj->t_ca : SB_MIN_LEAD_S;

    if (conj->miss_distance > SB_EPS_LATERAL_M)
    {
        u_away_eci = sb_vscale(conj->miss_vector,
                               (-1.0 / conj->miss_distance));
    }
    else
    {
        const double dvn = sb_vnorm(conj->rel_velocity);
        Vec3 axis;
        Vec3 p;
        if (dvn > 1.0e-9)
        {
            axis = sb_vdiv(conj->rel_velocity, dvn);
        }
        else
        {
            axis = sb_vnormalized(self_now.position);
        }
        p = sb_pinned_perpendicular(axis);
        u_away_eci = self_is_canonical_lo ? p : sb_vscale(p, -1.0);
    }
    d_needed = (conj->miss_distance < target)
                   ? (target - conj->miss_distance)
                   : target;

    basis_tca = sb_hill_basis(self_tca);
    u_hill = sb_mat3_mul(&basis_tca, u_away_eci);
    phi = sb_cw_phi_rv(n, tau);
    w = sb_mat3_tmul(&phi, u_hill);
    w2 = sb_vdot(w, w);

    if (w2 > 1.0e-12)
    {
        plan.dv_hill = sb_vscale(w, (d_needed / w2));
    }
    else
    {
        plan.dv_hill = sb_vscale(u_hill, (d_needed / tau));
    }
    basis_now = sb_hill_basis(self_now);
    plan.dv_eci = sb_mat3_tmul(&basis_now, plan.dv_hill);
    plan.dv_mag = sb_vnorm(plan.dv_hill);
    plan.displacement_m = sb_vdot(sb_mat3_mul(&phi, plan.dv_hill), u_hill);
    return plan;
}

MirrorReturnPlan sb_plan_mirror_return(Vec3 giveway_dv_hill, double n,
                                       int phasing_orbits)
{
    MirrorReturnPlan plan;
    const double period = (2.0 * SB_PI) / n;
    const double y_off = ((-6.0 * SB_PI) * giveway_dv_hill.y) / n;
    const double dv_p =
        (y_off * n) / ((6.0 * SB_PI) * (double)phasing_orbits);
    Vec3 phase;
    phase.x = 0.0;
    phase.y = dv_p;
    phase.z = 0.0;
    plan.mirror_dv_hill = sb_vadd(sb_vscale(giveway_dv_hill, -1.0), phase);
    plan.final_dv_hill = sb_vscale(phase, -1.0);
    plan.wait_s = period;
    plan.transfer_s = period * (double)phasing_orbits;
    plan.total_dv = sb_vnorm(plan.mirror_dv_hill) +
                    sb_vnorm(plan.final_dv_hill);
    return plan;
}

ReturnPlan sb_plan_return(HillState offset, double n)
{
    ReturnPlan plan;
    Mat3 rr;
    Mat3 rv;
    Mat3 vr;
    Mat3 vv;
    Vec3 v_required;
    Vec3 v_arrival;

    plan.transfer_s = (0.5 * SB_PI) / n;
    rr = sb_cw_phi_rr(n, plan.transfer_s);
    rv = sb_cw_phi_rv(n, plan.transfer_s);
    vr = sb_cw_phi_vr(n, plan.transfer_s);
    vv = sb_cw_phi_vv(n, plan.transfer_s);

    v_required = sb_mat3_solve(&rv, sb_vscale(sb_mat3_mul(&rr, offset.r),
                                               -1.0));
    plan.dv1_hill = sb_vsub(v_required, offset.v);
    v_arrival = sb_vadd(sb_mat3_mul(&vr, offset.r),
                        sb_mat3_mul(&vv, v_required));
    plan.dv2_hill = sb_vscale(v_arrival, -1.0);
    plan.total_dv = sb_vnorm(plan.dv1_hill) + sb_vnorm(plan.dv2_hill);
    return plan;
}
