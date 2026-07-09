// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — Layer 6: the give-way maneuver and return-to-slot (SPEC §6).
//
// Minimum-norm give-way burn. Only separation along the miss direction
// matters, so the constraint is scalar:  u' * Phi_rv(tau) * dv = d_needed,
// and the minimum-|dv| solution is the analytic pseudoinverse
//
//     dv = d_needed * (Phi_rv' u) / |Phi_rv' u|^2                (SPEC §6.4)
//
// evaluated in the give-way craft's Hill frame with Clohessy-Wiltshire
// dynamics — one line, no iteration, flight-suitable.
//
// Fail-safe under double give-way (Theorem 2, SPEC §5.9): each craft pushes
// AWAY along its own view of the miss vector, and those views are exact
// negations of each other, so if BOTH craft maneuver (trust divergence,
// disputed verdict) their displacements oppose and separation still grows.
// The degenerate zero-miss case preserves this by signing the cross-track
// push with the canonical pair order.
//
// Return-to-slot (Layer 3) is the classical two-impulse CW transfer back to
// the assigned slot over a pinned quarter-period, which is never singular
// (SPEC §6.5). The full sequence restores the original orbit exactly in CW
// dynamics — the maneuver is analytically reversible.
// ============================================================================

#include "covariance.hpp"
#include "frames.hpp"

namespace starboard {

struct GiveWayPlan {
    bool   required;       // false if the pair is already outside kappa*R_SAFE
    Vec3   dv_eci;         // the burn (m/s), ECI
    Vec3   dv_hill;        // the same burn in the craft's Hill frame
    double dv_mag;         // |dv| (m/s)
    double displacement_m; // achieved separation increase at TCA (CW-exact)
};

// Plan the give-way burn for `self` against a conjunction expressed in
// self's perspective (miss_vector = other relative to self at TCA).
//
// The Hill frame ROTATES with the reference orbit, so the required
// displacement direction must be expressed in the frame AT TCA (`self_tca`,
// from screening), while the resulting impulse converts back to ECI through
// the frame at burn time (`self_now`). Conflating the two frames is only
// harmless for short leads (n*tau << 1); the screener hands both states over
// precisely so this is exact at any lead time.
//
// `self_is_canonical_lo` = (id_compare(self, other) <= 0); it signs the
// cross-track fallback in the zero-miss degenerate case.
inline GiveWayPlan plan_give_way(const CraftState& self_now,
                                 const CraftState& self_tca,
                                 const Conjunction& conj,
                                 bool self_is_canonical_lo,
                                 double r_safe = canon::R_SAFE_M) {
    GiveWayPlan plan;
    const double target = canon::MANEUVER_MARGIN * r_safe;
    const bool pc_hot = (conj.pc >= canon::PC_ACTIONABLE);
    if ((conj.miss_distance >= target) && !pc_hot) {
        plan.required = false;
        plan.dv_eci = Vec3{};
        plan.dv_hill = Vec3{};
        plan.dv_mag = 0.0;
        plan.displacement_m = 0.0;
        return plan;
    }
    plan.required = true;

    const double n = mean_motion(self_now);
    const double tau =
        (conj.t_ca > canon::MIN_LEAD_S) ? conj.t_ca : canon::MIN_LEAD_S;

    // Push direction: directly away from the other craft's TCA position.
    Vec3 u_away_eci;
    if (conj.miss_distance > canon::EPS_LATERAL_M) {
        u_away_eci = conj.miss_vector * (-1.0 / conj.miss_distance);
    } else {
        // Degenerate near-zero miss: no natural away direction. Build the
        // PINNED perpendicular of the SHARED encounter axis. Both craft see
        // rel_velocity as exact negations, and pinned_perpendicular(n) ==
        // pinned_perpendicular(-n), so both derive the SAME direction p;
        // signing by canonical order then guarantees OPPOSITE pushes
        // (Theorem 2) in every geometry — including counter-rotating
        // head-on, where an own-frame cross-track direction fails to oppose
        // (chat_hi ~ -chat_lo makes both push the same inertial way).
        const double dvn = conj.rel_velocity.norm();
        const Vec3 axis = (dvn > 1.0e-9)
                              ? (conj.rel_velocity / dvn)
                              : self_now.position.normalized();
        const Vec3 p = detail::pinned_perpendicular(axis);
        u_away_eci = self_is_canonical_lo ? p : (p * -1.0);
    }
    // Geometric shortfall when inside the target; when actionable on Pc
    // alone (large covariance, miss already >= target) open by a full margin
    // quantum — a deterministic, monotonic risk reduction — and re-screen
    // after execution (SPEC §6.4): previously this case planned NO burn and
    // the protocol dead-ended with the risk unmitigated.
    const double d_needed = (conj.miss_distance < target)
                                ? (target - conj.miss_distance)
                                : target;

    const Vec3 u_hill = hill_basis(self_tca).mul(u_away_eci);
    const Mat3 phi = cw_phi_rv(n, tau);
    const Vec3 w = phi.tmul(u_hill);          // Phi_rv' u
    const double w2 = w.dot(w);

    if (w2 > 1.0e-12) {
        plan.dv_hill = w * (d_needed / w2);   // analytic min-norm solution
    } else {
        // Phi_rv' u ~ 0 only at pathological (n*tau) resonances with a pure
        // cross-track u; fall back to the linear-motion impulse.
        plan.dv_hill = u_hill * (d_needed / tau);
    }
    plan.dv_eci = hill_basis(self_now).tmul(plan.dv_hill);
    plan.dv_mag = plan.dv_hill.norm();
    plan.displacement_m = phi.mul(plan.dv_hill).dot(u_hill);
    return plan;
}

// --- Canonical return-to-slot: MIRROR + PHASE (SPEC §6.5) -------------------
// CW identity: one full period after ANY impulse dv, the position offset is
// purely along-track, y_off = -6*pi*dv_y/n, and the velocity offset is again
// exactly dv (Phi_rv(2pi) kills radial/cross rows; Phi_vv(2pi) = I). So the
// analytically exact, energy-symmetric undo is:
//   B1 (at burn + 1 period):  -dv + dv_p   (mirror restores the orbit energy;
//                                           dv_p starts the phasing drift)
//   B2 (N periods later):     -dv_p        (arrive on-slot, on-velocity)
// with dv_p = y_off*n/(6*pi*N). Total return cost = |dv| + 2*|dv_y|/N — the
// give-way burn is reversible by construction, and the whole campaign costs
// dv*(2 + 2/N) when the give-way burn is tangential (Theorem 3).
struct MirrorReturnPlan {
    Vec3   mirror_dv_hill;  // B1 = -dv_giveway + phasing component
    Vec3   final_dv_hill;   // B2 = -phasing component
    double wait_s;          // one period between give-way burn and B1
    double transfer_s;      // N periods between B1 and B2
    double total_dv;
};

inline MirrorReturnPlan plan_mirror_return(const Vec3& giveway_dv_hill,
                                           double n, int phasing_orbits) {
    MirrorReturnPlan plan;
    const double period = (2.0 * canon::PI) / n;
    const double y_off = -6.0 * canon::PI * giveway_dv_hill.y / n;
    const double dv_p =
        (y_off * n) / (6.0 * canon::PI * static_cast<double>(phasing_orbits));
    const Vec3 phase(0.0, dv_p, 0.0);
    plan.mirror_dv_hill = (giveway_dv_hill * -1.0) + phase;
    plan.final_dv_hill = phase * -1.0;
    plan.wait_s = period;
    plan.transfer_s = period * static_cast<double>(phasing_orbits);
    plan.total_dv = plan.mirror_dv_hill.norm() + plan.final_dv_hill.norm();
    return plan;
}

// Alternative: two-impulse forced CW transfer back to the slot, from an
// arbitrary offset, over a pinned quarter period (n*tau = pi/2, where Phi_rv
// is provably non-singular). Fast but fuel-expensive (~n*|offset|); use when
// the slot must be regained quickly. The mirror+phase return above is the
// canonical choice.
struct ReturnPlan {
    Vec3   dv1_hill;    // depart burn (m/s)
    Vec3   dv2_hill;    // arrival null burn (m/s)
    double transfer_s;  // quarter period
    double total_dv;    // |dv1| + |dv2|
};

inline ReturnPlan plan_return(const HillState& offset, double n) {
    ReturnPlan plan;
    plan.transfer_s = (0.5 * canon::PI) / n;

    const Mat3 rr = cw_phi_rr(n, plan.transfer_s);
    const Mat3 rv = cw_phi_rv(n, plan.transfer_s);
    const Mat3 vr = cw_phi_vr(n, plan.transfer_s);
    const Mat3 vv = cw_phi_vv(n, plan.transfer_s);

    // Require r(tau) = 0:  rr*r0 + rv*(v0 + dv1) = 0.
    const Vec3 v_required = mat3_solve(rv, rr.mul(offset.r) * -1.0);
    plan.dv1_hill = v_required - offset.v;

    // Null the arrival velocity: back on-slot, on-velocity — orbit restored.
    const Vec3 v_arrival = vr.mul(offset.r) + vv.mul(v_required);
    plan.dv2_hill = v_arrival * -1.0;

    plan.total_dv = plan.dv1_hill.norm() + plan.dv2_hill.norm();
    return plan;
}

} // namespace starboard
