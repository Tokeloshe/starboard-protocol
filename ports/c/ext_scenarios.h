// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// ext_scenarios.h — scenario construction for the extended transcript.
// Host test tooling only. Every draw uses the suite LCG and every geometric
// construction uses only + - * / sqrt, so the scenarios themselves are
// bit-identical on every IEEE-754 platform (no libm trig, unlike the v1
// transcript scaffolding). conformance/ext_bits.cpp mirrors this file
// draw-for-draw; the draw ORDER is part of the transcript definition.

#ifndef STARBOARD_EXT_SCENARIOS_H
#define STARBOARD_EXT_SCENARIOS_H

#include "starboard.h"
#include <math.h>
#include <stdio.h>

#define EXT_EPOCH_MS (1401000000000ULL)

static double ext_uniform(uint64_t *s)
{
    *s = (*s * 6364136223846793005ULL) + 1442695040888963407ULL;
    return (double)(*s >> 11) / 9007199254740992.0;
}

static double ext_sym(uint64_t *s)
{
    return (2.0 * ext_uniform(s)) - 1.0;
}

static Vec3 ext_dir(uint64_t *s)
{
    Vec3 v;
    v.x = ext_sym(s);
    v.y = ext_sym(s);
    v.z = ext_sym(s) + 0.125;   /* keep away from the zero vector */
    return sb_vnormalized(v);
}

/* A near-circular LEO state: random plane, 350..1250 km altitude. */
static CraftState ext_orbit_state(uint64_t *s)
{
    const double r = SB_R_EARTH_M + 350.0e3 + (900.0e3 * ext_uniform(s));
    const Vec3 rhat = ext_dir(s);
    const Vec3 any = ext_dir(s);
    const Vec3 t = sb_vsub(any, sb_vscale(rhat, sb_vdot(any, rhat)));
    const double v = sqrt(SB_MU_EARTH_M3S2 / r) *
                     (1.0 + (0.002 * ext_sym(s)));
    CraftState st;
    st.position = sb_vscale(rhat, r);
    st.velocity = sb_vscale(sb_vnormalized(t), v);
    return st;
}

static CraftState ext_linear_at(CraftState st, double t)
{
    CraftState r;
    r.position = sb_vadd(st.position, sb_vscale(st.velocity, t));
    r.velocity = st.velocity;
    return r;
}

/* A crossing encounter in the linear kernel model: b passes a's TCA point
 * with a random miss (0..1500 m) at a random crossing angle; both are then
 * backed off linearly by t_ca. */
static void ext_encounter(uint64_t *s, CraftState *a, CraftState *b,
                          double *t_ca)
{
    const CraftState a0 = ext_orbit_state(s);
    const Vec3 rhat = sb_vnormalized(a0.position);
    const Vec3 vhat = sb_vnormalized(a0.velocity);
    const Vec3 chat = sb_vcross(rhat, vhat);
    const double cx = ext_sym(s);
    const double sx = ext_sym(s);
    const Vec3 dir = sb_vnormalized(sb_vadd(sb_vscale(vhat, cx),
                                            sb_vscale(chat, sx)));
    const double miss = 1500.0 * ext_uniform(s);
    const Vec3 mdir = ext_dir(s);
    CraftState b0;
    *t_ca = 100.0 + (800.0 * ext_uniform(s));
    b0.position = sb_vadd(a0.position, sb_vscale(mdir, miss));
    b0.velocity = sb_vscale(dir, sb_vnorm(a0.velocity));
    *a = ext_linear_at(a0, -*t_ca);
    *b = ext_linear_at(b0, -*t_ca);
}

static void ext_descriptor(uint64_t *s, int w, int k, CraftDescriptor *d)
{
    char id[16];
    const double u_tier = ext_uniform(s);
    const double u_cap = ext_uniform(s);
    const double u_trust = ext_uniform(s);
    const double var = 1.0e3 + (ext_uniform(s) * 1.0e6);
    (void)snprintf(id, sizeof(id), "20%02d-%03d%c", w % 27, (w * 7) % 1000,
                   (char)('A' + k));
    d->object_id = sb_make_object_id(id);
    d->tier = (Tier)(int)(u_tier * 4.0);
    d->can_maneuver = (u_cap < 0.7);
    d->trust = (TrustLevel)(int)(u_trust * 3.0);
    d->hard_body_radius_m = SB_DEFAULT_HBR_M;
    d->state = sb_canonicalize(ext_orbit_state(s));
    d->covariance = sb_sym33_diagonal(var);
}

#endif /* STARBOARD_EXT_SCENARIOS_H */
