// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// flock.c — Layer 4, the flock: maneuver notices, gossip ledger, and the
// flock check (starboard_flock.h). Allocation-free, recursion-free; every
// entry point is bounded by SB_FLOCK_MAX_ENTRIES and SB_FLOCK_MAX_WINDOW_S.

#include "starboard_flock.h"
#include "starboard_fsw.h"     /* sb_crc32 */
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Little-endian codec                                                      */
/* ------------------------------------------------------------------------ */

static uint64_t bits(double d)
{
    uint64_t u;
    (void)memcpy(&u, &d, sizeof(u));
    return u;
}

static double from_bits(uint64_t u)
{
    double d;
    (void)memcpy(&d, &u, sizeof(d));
    return d;
}

static uint32_t put_u(uint8_t *o, uint64_t v, uint32_t n)
{
    uint32_t i;
    for (i = 0U; i < n; ++i)
    {
        o[i] = (uint8_t)(v >> (8U * i));
    }
    return n;
}

static uint64_t get_u(const uint8_t *p, uint32_t n)
{
    uint64_t v = 0U;
    uint32_t i;
    for (i = 0U; i < n; ++i)
    {
        v |= (uint64_t)p[i] << (8U * i);
    }
    return v;
}

static uint32_t put_vec(uint8_t *o, Vec3 v)
{
    uint32_t k = put_u(o, bits(v.x), 8U);
    k += put_u(&o[k], bits(v.y), 8U);
    k += put_u(&o[k], bits(v.z), 8U);
    return k;
}

static Vec3 get_vec(const uint8_t *p)
{
    Vec3 v;
    v.x = from_bits(get_u(p, 8U));
    v.y = from_bits(get_u(&p[8], 8U));
    v.z = from_bits(get_u(&p[16], 8U));
    return v;
}

uint32_t sb_notice_encode(const SbNotice *n, uint8_t out[SB_NOTICE_BYTES])
{
    uint32_t k = 0U;
    uint32_t crc;
    out[0] = 'S';
    out[1] = 'B';
    out[2] = 'F';
    out[3] = 'N';
    out[4] = (uint8_t)SB_NOTICE_VERSION;
    out[5] = 0U;                         /* flags (reserved) */
    out[6] = n->hops;
    out[7] = 0U;
    k = 8U;
    k += put_u(&out[k], SB_RULESET_ID, 4U);
    (void)memcpy(&out[k], n->origin.c, 12U);
    k += 12U;
    k += put_u(&out[k], n->seq, 4U);
    k += put_u(&out[k], n->conjunction_id, 8U);
    (void)memcpy(&out[k], n->counterpart.c, 12U);
    k += 12U;
    k += put_u(&out[k], (uint64_t)n->tca_epoch_ms, 8U);
    k += put_u(&out[k], (uint64_t)n->burn_epoch_ms, 8U);
    k += put_vec(&out[k], n->post_burn.position);
    k += put_vec(&out[k], n->post_burn.velocity);
    k += put_vec(&out[k], n->dv_eci);
    k += put_u(&out[k], (uint64_t)n->mirror_epoch_ms, 8U);
    k += put_vec(&out[k], n->mirror_dv_hill);
    k += put_u(&out[k], (uint64_t)n->final_epoch_ms, 8U);
    k += put_vec(&out[k], n->final_dv_hill);
    /* The CRC excludes the hop byte, which relays decrement. */
    out[6] = 0U;
    crc = sb_crc32(out, k);
    out[6] = n->hops;
    k += put_u(&out[k], crc, 4U);
    return k;
}

static bool finite_d(double x)
{
    return (x - x) == 0.0;
}

static bool finite_v(Vec3 v)
{
    return finite_d(v.x) && finite_d(v.y) && finite_d(v.z);
}

bool sb_notice_decode(const uint8_t *in, uint32_t len, SbNotice *out)
{
    uint8_t tmp[SB_NOTICE_BYTES];
    uint32_t crc;
    double rn;
    double vn;
    (void)memset(out, 0, sizeof(*out));
    if ((len != SB_NOTICE_BYTES) || (in[0] != 'S') || (in[1] != 'B') ||
        (in[2] != 'F') || (in[3] != 'N') || (in[4] != SB_NOTICE_VERSION) ||
        (get_u(&in[8], 4U) != SB_RULESET_ID))
    {
        return false;
    }
    (void)memcpy(tmp, in, SB_NOTICE_BYTES);
    tmp[6] = 0U;
    crc = sb_crc32(tmp, SB_NOTICE_BYTES - 4U);
    if (crc != (uint32_t)get_u(&in[SB_NOTICE_BYTES - 4U], 4U))
    {
        return false;
    }
    out->hops = in[6];
    (void)memcpy(out->origin.c, &in[12], 12U);
    out->seq = (uint32_t)get_u(&in[24], 4U);
    out->conjunction_id = get_u(&in[28], 8U);
    (void)memcpy(out->counterpart.c, &in[36], 12U);
    out->tca_epoch_ms = (int64_t)get_u(&in[48], 8U);
    out->burn_epoch_ms = (int64_t)get_u(&in[56], 8U);
    out->post_burn.position = get_vec(&in[64]);
    out->post_burn.velocity = get_vec(&in[88]);
    out->dv_eci = get_vec(&in[112]);
    out->mirror_epoch_ms = (int64_t)get_u(&in[136], 8U);
    out->mirror_dv_hill = get_vec(&in[144]);
    out->final_epoch_ms = (int64_t)get_u(&in[168], 8U);
    out->final_dv_hill = get_vec(&in[176]);
    /* Domain: finite, physical, causally ordered (SPEC §3.7). */
    if (!(finite_v(out->post_burn.position) &&
          finite_v(out->post_burn.velocity) && finite_v(out->dv_eci) &&
          finite_v(out->mirror_dv_hill) && finite_v(out->final_dv_hill)))
    {
        return false;
    }
    rn = sb_vnorm(out->post_burn.position);
    vn = sb_vnorm(out->post_burn.velocity);
    if ((rn < 6.0e6) || (rn > 5.0e7) || (vn < 1.0e3) || (vn > 2.0e4) ||
        (sb_vnorm(out->dv_eci) > 100.0) || (out->burn_epoch_ms < 0) ||
        (out->mirror_epoch_ms < out->burn_epoch_ms) ||
        (out->final_epoch_ms < out->mirror_epoch_ms) ||
        ((double)(out->final_epoch_ms - out->burn_epoch_ms) >
         (1000.0 * SB_PROP_DT_MAX_S)))
    {
        return false;
    }
    return true;
}

void sb_notice_from_plan(const ObjectId *self, const ObjectId *counterpart,
                         uint32_t seq, uint64_t conjunction_id,
                         int64_t tca_epoch_ms, int64_t burn_epoch_ms,
                         CraftState self_tca, const GiveWayPlan *plan,
                         const MirrorReturnPlan *ret, SbNotice *out)
{
    const double tau = (double)(tca_epoch_ms - burn_epoch_ms) / 1000.0;
    CraftState at_burn = sb_propagate(self_tca, -tau);
    (void)memset(out, 0, sizeof(*out));
    out->hops = (uint8_t)SB_FLOCK_DEFAULT_HOPS;
    out->origin = *self;
    out->seq = seq;
    out->conjunction_id = conjunction_id;
    out->counterpart = *counterpart;
    out->tca_epoch_ms = tca_epoch_ms;
    out->burn_epoch_ms = burn_epoch_ms;
    at_burn.velocity = sb_vadd(at_burn.velocity, plan->dv_eci);
    out->post_burn = sb_canonicalize(at_burn);
    out->dv_eci = plan->dv_eci;
    out->mirror_epoch_ms = burn_epoch_ms + (int64_t)(ret->wait_s * 1000.0);
    out->mirror_dv_hill = ret->mirror_dv_hill;
    out->final_epoch_ms = burn_epoch_ms +
        (int64_t)((ret->wait_s + ret->transfer_s) * 1000.0);
    out->final_dv_hill = ret->final_dv_hill;
}

/* ------------------------------------------------------------------------ */
/* Ledger                                                                   */
/* ------------------------------------------------------------------------ */

void sb_flock_init(SbFlock *f, ObjectId self_id)
{
    (void)memset(f, 0, sizeof(*f));
    f->self_id = self_id;
}

SbFlockResult sb_flock_receive(SbFlock *f, const uint8_t *in, uint32_t len,
                               uint8_t relay[SB_NOTICE_BYTES],
                               uint32_t *relay_len)
{
    SbNotice n;
    uint32_t i;
    int32_t free_slot = -1;
    int32_t match = -1;
    SbFlockResult res;
    *relay_len = 0U;
    if (!sb_notice_decode(in, len, &n) || sb_id_equal(&n.origin, &f->self_id))
    {
        ++f->rejected;
        return SB_FLOCK_REJECTED;
    }
    for (i = 0U; i < SB_FLOCK_MAX_ENTRIES; ++i)
    {
        const SbFlockEntry *e = &f->e[i];
        if (!e->used)
        {
            if (free_slot < 0)
            {
                free_slot = (int32_t)i;
            }
            continue;
        }
        if (sb_id_equal(&e->n.origin, &n.origin) &&
            (e->n.conjunction_id == n.conjunction_id))
        {
            match = (int32_t)i;
        }
    }
    if (match >= 0)
    {
        if (n.seq <= f->e[match].n.seq)
        {
            ++f->duplicates;
            return SB_FLOCK_DUPLICATE;
        }
        f->e[match].n = n;
        res = SB_FLOCK_UPDATED;
    }
    else
    {
        if (free_slot < 0)
        {
            ++f->rejected;
            return SB_FLOCK_FULL;
        }
        f->e[free_slot].used = true;
        f->e[free_slot].n = n;
        res = SB_FLOCK_NEW;
    }
    ++f->received;
    if (n.hops > 0U)
    {
        n.hops = (uint8_t)(n.hops - 1U);
        *relay_len = sb_notice_encode(&n, relay);
        ++f->relayed;
    }
    return res;
}

void sb_flock_expire(SbFlock *f, int64_t now_gps_ms, int64_t retain_ms)
{
    uint32_t i;
    for (i = 0U; i < SB_FLOCK_MAX_ENTRIES; ++i)
    {
        if (f->e[i].used && ((f->e[i].n.final_epoch_ms + retain_ms) <
                             now_gps_ms))
        {
            (void)memset(&f->e[i], 0, sizeof(f->e[i]));
        }
    }
}

uint64_t sb_flock_digest(const SbFlock *f)
{
    /* XOR of per-entry FNV digests: independent of slot order. The hop
     * count is excluded (it differs by path, not by content). */
    uint64_t acc = 0U;
    uint32_t i;
    for (i = 0U; i < SB_FLOCK_MAX_ENTRIES; ++i)
    {
        if (f->e[i].used)
        {
            SbNotice n = f->e[i].n;
            uint8_t b[SB_NOTICE_BYTES];
            uint64_t h = 14695981039346656037ULL;
            uint32_t k;
            n.hops = 0U;
            (void)sb_notice_encode(&n, b);
            for (k = 0U; k < SB_NOTICE_BYTES; ++k)
            {
                h ^= (uint64_t)b[k];
                h *= 1099511628211ULL;
            }
            acc ^= h;
        }
    }
    return acc;
}

/* ------------------------------------------------------------------------ */
/* Flock check                                                              */
/* ------------------------------------------------------------------------ */

static void min_in_step(CraftState a, CraftState b, double h, double t0,
                        double *best, double *best_t)
{
    /* Closest approach inside one step under locally linear relative
     * motion — catches fast crossings that fall between grid samples. */
    const Vec3 dr = sb_vsub(b.position, a.position);
    const Vec3 dv = sb_vsub(b.velocity, a.velocity);
    const double dv2 = sb_vdot(dv, dv);
    double t = 0.0;
    double d;
    if (dv2 > 0.0)
    {
        t = -sb_vdot(dr, dv) / dv2;
        if (t < 0.0)
        {
            t = 0.0;
        }
        if (t > h)
        {
            t = h;
        }
    }
    d = sb_vnorm(sb_vadd(dr, sb_vscale(dv, t)));
    if (d < *best)
    {
        *best = d;
        *best_t = t0 + t;
    }
}

static CraftState apply_hill_burn(CraftState s, Vec3 dv_hill)
{
    const Mat3 b = sb_hill_basis(s);
    s.velocity = sb_vadd(s.velocity, sb_mat3_tmul(&b, dv_hill));
    return s;
}

/* One trajectory under a notice's burn schedule, marched on a shared clock
 * (t = seconds after the mover's burn epoch). */
typedef struct {
    CraftState s;
    double     t_mirror;
    double     t_final;
    Vec3       mirror_dv;
    Vec3       final_dv;
    bool       mirror_done;
    bool       final_done;
} Track;

static void track_from_notice(Track *k, const SbNotice *n, double t0_offset)
{
    k->s = n->post_burn;
    k->t_mirror = ((double)(n->mirror_epoch_ms - n->burn_epoch_ms) / 1000.0) +
                  t0_offset;
    k->t_final = ((double)(n->final_epoch_ms - n->burn_epoch_ms) / 1000.0) +
                 t0_offset;
    k->mirror_dv = n->mirror_dv_hill;
    k->final_dv = n->final_dv_hill;
    k->mirror_done = false;
    k->final_done = false;
}

static double track_next_event(const Track *k)
{
    if (!k->mirror_done)
    {
        return k->t_mirror;
    }
    if (!k->final_done)
    {
        return k->t_final;
    }
    return 1.0e300;
}

static void track_fire(Track *k, double t)
{
    if ((!k->mirror_done) && (t >= k->t_mirror))
    {
        k->s = apply_hill_burn(k->s, k->mirror_dv);
        k->mirror_done = true;
    }
    if (k->mirror_done && (!k->final_done) && (t >= k->t_final))
    {
        k->s = apply_hill_burn(k->s, k->final_dv);
        k->final_done = true;
    }
}

SbFlockCheck sb_flock_check_pair(const SbNotice *n, const SbNotice *own_plan,
                                 CraftState own, int64_t own_epoch_ms)
{
    SbFlockCheck r;
    Track m;
    Track me;
    double t = 0.0;
    double end_s;
    double best = 1.0e300;
    double best_t = 0.0;
    bool own_moves = false;

    r.conflict = false;
    r.min_sep_m = 1.0e300;
    r.at_epoch_ms = n->burn_epoch_ms;
    track_from_notice(&m, n, 0.0);
    if ((own_plan != NULL) && (own_plan->burn_epoch_ms == n->burn_epoch_ms))
    {
        /* Unison maneuver: both burn at the same epoch. */
        track_from_notice(&me, own_plan, 0.0);
        own_moves = true;
    }
    else
    {
        const double dt0 = (double)(n->burn_epoch_ms - own_epoch_ms) / 1000.0;
        const double adt = (dt0 < 0.0) ? -dt0 : dt0;
        if (adt > SB_PROP_DT_MAX_S)
        {
            return r;            /* out of domain: caller's state too old */
        }
        me.s = sb_propagate(sb_canonicalize(own), dt0);
        me.t_mirror = 1.0e300;
        me.t_final = 1.0e300;
        me.mirror_done = true;
        me.final_done = true;
    }
    end_s = m.t_final;
    if (own_moves && (me.t_final > end_s))
    {
        end_s = me.t_final;
    }
    end_s += SB_SCREEN_ARC_MAX_S;
    if (end_s > SB_FLOCK_MAX_WINDOW_S)
    {
        end_s = SB_FLOCK_MAX_WINDOW_S;
    }
    while (t < end_s)
    {
        double h = SB_FLOCK_STEP_S;
        const double ev_m = track_next_event(&m);
        const double ev_o = track_next_event(&me);
        if ((t + h) > ev_m)
        {
            h = ev_m - t;
        }
        if ((t + h) > ev_o)
        {
            h = ev_o - t;
        }
        if (h > 0.0)
        {
            min_in_step(m.s, me.s, h, t, &best, &best_t);
            sb_rk4_step(&m.s, h);
            sb_rk4_step(&me.s, h);
            t += h;
        }
        track_fire(&m, t);
        track_fire(&me, t);
    }
    r.min_sep_m = best;
    r.at_epoch_ms = n->burn_epoch_ms + (int64_t)(best_t * 1000.0);
    r.conflict = (best < SB_R_SAFE_M);
    return r;
}

SbFlockCheck sb_flock_check(const SbNotice *n, CraftState own,
                            int64_t own_epoch_ms)
{
    return sb_flock_check_pair(n, NULL, own, own_epoch_ms);
}
