// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// easy.c — the five-minute API (starboard_easy.h). A thin layer over
// sb_cdm_parse + sb_fsw_decide: it adds profiles, identity matching and a
// plain-English summary. It makes no decision of its own, so its answers
// are exactly the flight layer's. No heap, no stdio, no globals written
// except the context of sb_easy_selftest.

#include "starboard_easy.h"
#include <string.h>

typedef struct {
    Tier     tier;
    bool     can_maneuver;
    int64_t  burn_lead_ms;     /* preferred lead (flight-layer default)   */
    int64_t  cmd_latency_ms;
    int64_t  lead_min_ms;      /* window searched for the cheapest burn   */
    int64_t  lead_max_ms;
} SbProfileRow;

/* Indexed by SbSatProfile. The preferred lead is about half an orbit, where
 * a give-way burn is usually cheapest. The burn cost is NOT monotonic in
 * lead time (a burn exactly one orbit ahead can cost 10x more for in-plane
 * crossings), so sb_easy_decide searches each profile's window on a fixed
 * grid for the cheapest burn. Windows widen where commands go up on ground
 * passes (CubeSat) or the vehicle plans conservatively (high-value, crewed). */
static const SbProfileRow sb_profiles[SB_SAT_PROFILE_COUNT] = {
    { TIER_OPERATIONAL, false, 2700000,  60000,  2700000,  2700000 },
    { TIER_OPERATIONAL, true,  2700000, 600000,  3600000, 16200000 },
    { TIER_OPERATIONAL, true,  2700000, 120000,  1800000, 10800000 },
    { TIER_OPERATIONAL, true,  2700000,  60000,  1200000,  8100000 },
    { TIER_CRITICAL,    true,  2700000, 120000,  2700000, 16200000 },
    { TIER_CREWED,      true,  2700000, 300000,  2700000, 16200000 }
};

#define SB_EASY_LEAD_STEP_MS (300000)   /* 5-minute search grid */

/* ---- a tiny bounded string builder (no stdio on flight code) ---------- */

typedef struct {
    char    *p;
    uint32_t cap;
    uint32_t n;
} Sb;

static void sb_put(Sb *s, const char *t)
{
    uint32_t i = 0U;
    while ((t[i] != '\0') && ((s->n + 1U) < s->cap))
    {
        s->p[s->n] = t[i];
        ++s->n;
        ++i;
    }
    s->p[s->n] = '\0';
}

static void sb_put_n(Sb *s, const char *t, uint32_t len)
{
    uint32_t i = 0U;
    while ((i < len) && (t[i] != '\0') && ((s->n + 1U) < s->cap))
    {
        s->p[s->n] = t[i];
        ++s->n;
        ++i;
    }
    s->p[s->n] = '\0';
}

static const char sb_digits[11] = "0123456789";

static void sb_put_u(Sb *s, uint64_t value)
{
    char d[24];
    char out[24];
    uint64_t v = value;
    uint32_t k = 0U;
    uint32_t j = 0U;
    do
    {
        d[k] = sb_digits[v % 10U];
        ++k;
        v /= 10U;
    } while ((v != 0U) && (k < 20U));
    while (k > 0U)
    {
        --k;
        out[j] = d[k];
        ++j;
    }
    out[j] = '\0';
    sb_put(s, out);
}

#define SB_FIXED_MAX_DEC (4U)

/* Fixed-point with `decimals` decimals (at most 4); |x| clamped to 1e12. */
static void sb_put_fixed(Sb *s, double x, uint32_t decimals, bool sign)
{
    const uint32_t dec = (decimals > SB_FIXED_MAX_DEC) ? SB_FIXED_MAX_DEC
                                                       : decimals;
    double scale = 1.0;
    double ax = (x < 0.0) ? -x : x;
    uint64_t q;
    uint64_t fp;
    uint64_t p10 = 1U;
    uint32_t i;
    if (!(ax == ax))
    {
        sb_put(s, "nan");
        return;
    }
    if (ax > 1.0e12)
    {
        ax = 1.0e12;
    }
    for (i = 0U; i < dec; ++i)
    {
        scale *= 10.0;
        p10 *= 10U;
    }
    q = (uint64_t)((ax * scale) + 0.5);
    fp = q % p10;
    if ((x < 0.0) && (q != 0U))
    {
        sb_put(s, "-");
    }
    else if (sign)
    {
        sb_put(s, "+");
    }
    else
    {
        /* no sign */
    }
    sb_put_u(s, q / p10);
    if (dec > 0U)
    {
        char frac[SB_FIXED_MAX_DEC + 1U];
        sb_put(s, ".");
        for (i = dec; i > 0U; --i)
        {
            frac[i - 1U] = sb_digits[fp % 10U];
            fp /= 10U;
        }
        frac[dec] = '\0';
        sb_put(s, frac);
    }
}

/* x as d.de-N (x in (0, 1]); used for probabilities. */
static void sb_put_sci(Sb *s, double value)
{
    double x = value;
    uint32_t down = 0U;     /* decimal exponent = -down (x < 1) */
    uint32_t k;
    if (!(x > 0.0))
    {
        sb_put(s, "0");
        return;
    }
    for (k = 0U; k < 400U; ++k)
    {
        if (x >= 1.0)
        {
            break;
        }
        x *= 10.0;
        ++down;
    }
    if (x >= 9.95)
    {
        x /= 10.0;
        if (down > 0U)
        {
            --down;
        }
    }
    sb_put_fixed(s, x, 1U, false);
    if (down != 0U)
    {
        sb_put(s, "e-");
        sb_put_u(s, down);
    }
}

/* Lead time as "45 min" / "2 h 05 min" / "30 s". */
static void sb_put_lead(Sb *s, int64_t ms)
{
    const uint64_t sec = (ms < 0) ? 0U : ((uint64_t)ms / 1000U);
    const uint64_t mins = (sec + 30U) / 60U;
    if (sec < 120U)
    {
        sb_put_u(s, sec);
        sb_put(s, " s");
    }
    else if (sec < 7200U)
    {
        sb_put_u(s, mins);
        sb_put(s, " min");
    }
    else
    {
        const uint64_t h = mins / 60U;
        const uint64_t m = mins % 60U;
        sb_put_u(s, h);
        sb_put(s, " h ");
        if (m < 10U)
        {
            sb_put(s, "0");
        }
        sb_put_u(s, m);
        sb_put(s, " min");
    }
}

/* ---- helpers ----------------------------------------------------------- */

static char up(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - ('a' - 'A')) : c;
}

static bool str_ieq(const char *a, const char *b, uint32_t bmax)
{
    uint32_t i = 0U;
    while ((i < bmax) && (b[i] != '\0') && (a[i] != '\0'))
    {
        if (up(a[i]) != up(b[i]))
        {
            return false;
        }
        ++i;
    }
    return (a[i] == '\0') && ((i == bmax) || (b[i] == '\0'));
}

static void copy_id(const ObjectId *id, char out[13])
{
    (void)memcpy(out, id->c, 12U);
    out[12] = '\0';
}

static bool matches(const SbCdmObject *o, const char *me)
{
    ObjectId want;
    want = sb_make_object_id(me);
    return sb_id_equal(&o->id, &want) ||
           str_ieq(me, o->designator, (uint32_t)sizeof(o->designator)) ||
           str_ieq(me, o->name, (uint32_t)sizeof(o->name));
}

static const char *rule_text(int r)
{
    switch (r)
    {
        case 1:  return "capability rule (only one craft can maneuver)";
        case 2:  return "priority rule (higher tier stands on)";
        case 3:  return "overtaking rule";
        case 4:  return "starboard rule";
        case 5:  return "fair-coin rule (shared deterministic tiebreak)";
        case 6:  return "multi-object cascade";
        default: return "none";
    }
}

static const char *fsw_err_text(SbFswStatus st)
{
    switch (st)
    {
        case SB_FSW_ERR_NOT_INVOLVED:
            return "this warning does not name your spacecraft (check `me`)";
        case SB_FSW_ERR_ARG:
            return "invalid argument";
        default:
            return "decision failed";
    }
}

static SbAction fail(SbAdvice *out, int code, uint32_t line, const char *txt)
{
    Sb s;
    out->action = SB_ACTION_ERROR;
    out->error = code;
    out->error_line = line;
    out->error_text = txt;
    s.p = out->summary;
    s.cap = (uint32_t)sizeof(out->summary);
    s.n = 0U;
    out->summary[0] = '\0';
    sb_put(&s, "WARNING NOT READ: ");
    sb_put(&s, txt);
    if (line != 0U)
    {
        sb_put(&s, " (line ");
        sb_put_u(&s, line);
        sb_put(&s, ")");
    }
    sb_put(&s, ". Treat the conjunction as unresolved, not as safe.");
    return SB_ACTION_ERROR;
}

/* ---- API ---------------------------------------------------------------- */

void sb_easy_profile_config(SbSatProfile profile, SbFswConfig *cfg)
{
    const uint32_t p = ((uint32_t)profile < SB_SAT_PROFILE_COUNT)
                       ? (uint32_t)profile : (uint32_t)SB_SAT_SMALLSAT;
    (void)memset(cfg, 0, sizeof(*cfg));
    cfg->self_tier = sb_profiles[p].tier;
    cfg->self_can_maneuver = sb_profiles[p].can_maneuver;
    cfg->operating_mode = SB_MODE_ADVISORY;
    cfg->apid_base = 0x500U;
    cfg->burn_lead_ms = sb_profiles[p].burn_lead_ms;
    cfg->cmd_latency_ms = sb_profiles[p].cmd_latency_ms;
}

SbAction sb_easy_decide(const char *cdm, uint32_t len, const char *me,
                        SbSatProfile profile, int64_t now_gps_ms,
                        SbAdvice *out)
{
    int64_t now;
    SbCdm parsed;
    SbFswConfig cfg;
    SbCounterpart other;
    SbDecision d;
    SbCdmStatus cst;
    SbFswStatus fst;
    uint32_t line = 0U;
    uint32_t si;
    Sb s;

    if (out == NULL)
    {
        return SB_ACTION_ERROR;
    }
    (void)memset(out, 0, sizeof(*out));
    out->rule_text = rule_text(0);
    if ((cdm == NULL) || ((uint32_t)profile >= SB_SAT_PROFILE_COUNT))
    {
        return fail(out, 100 + (int)SB_FSW_ERR_ARG, 0U, "invalid argument");
    }
    cst = sb_cdm_parse(cdm, len, &parsed, &line);
    if (cst != SB_CDM_OK)
    {
        return fail(out, (int)cst, line, sb_cdm_status_str(cst));
    }
    if (me == NULL)
    {
        si = 0U;
    }
    else if (matches(&parsed.obj[0], me))
    {
        si = 0U;
    }
    else if (matches(&parsed.obj[1], me))
    {
        si = 1U;
    }
    else
    {
        return fail(out, 100 + (int)SB_FSW_ERR_NOT_INVOLVED, 0U,
                    fsw_err_text(SB_FSW_ERR_NOT_INVOLVED));
    }

    sb_easy_profile_config(profile, &cfg);
    cfg.self_id = parsed.obj[si].id;
    (void)memset(&other, 0, sizeof(other));   /* no registry: CDM claims */
    now = (now_gps_ms > 0) ? now_gps_ms
                           : (parsed.tca_gps_ms - 21600000);  /* TCA - 6 h */
    fst = sb_fsw_decide(&cfg, &parsed, &other, now, &d);
    if (fst != SB_FSW_OK)
    {
        return fail(out, 100 + (int)fst, 0U, fsw_err_text(fst));
    }

    /* Cheapest burn in the profile's lead window. The verdict does not
     * depend on the lead; only the plan does. Each candidate is the flight
     * layer's own plan for that lead, so the result stays a pure function of
     * shared data on a fixed grid: the ground, and any neighbour that knows
     * this craft's profile, recomputes the same burn. Ties keep the earlier
     * (shorter-lead) candidate; candidates that would be late are skipped
     * unless none is on time. */
    if ((d.role == (uint8_t)SB_ROLE_GIVE_WAY) && cfg.self_can_maneuver &&
        d.plan.required)
    {
        const SbProfileRow *row = &sb_profiles[(uint32_t)profile];
        int64_t lead;
        SbDecision cand;
        bool have = (d.late == 0U);
        for (lead = row->lead_min_ms; lead <= row->lead_max_ms;
             lead += SB_EASY_LEAD_STEP_MS)
        {
            cfg.burn_lead_ms = lead;
            if ((sb_fsw_decide(&cfg, &parsed, &other, now, &cand)
                 == SB_FSW_OK) && cand.plan.required && (cand.late == 0U))
            {
                if ((!have) || (cand.plan.dv_mag < d.plan.dv_mag))
                {
                    d = cand;
                    have = true;
                }
            }
        }
    }

    copy_id(&parsed.obj[si].id, out->self_id);
    copy_id(&parsed.obj[1U - si].id, out->other_id);
    (void)memcpy(out->other_name, parsed.obj[1U - si].name,
                 sizeof(out->other_name));
    out->other_name[sizeof(out->other_name) - 1U] = '\0';
    out->conjunction_id = d.conjunction_id;
    out->miss_m = d.miss_m;
    out->pc = d.pc;
    out->tca_gps_ms = d.tca_epoch_ms;
    out->rule = (int)d.rule;
    out->rule_text = rule_text((int)d.rule);
    out->advisory = (int)d.advisory;
    out->late = (int)d.late;

    if (d.role == (uint8_t)SB_ROLE_NONE)
    {
        out->action = SB_ACTION_NOTHING;
    }
    else if (d.role == (uint8_t)SB_ROLE_STAND_ON)
    {
        out->action = SB_ACTION_HOLD;
    }
    else if (!cfg.self_can_maneuver)
    {
        out->action = SB_ACTION_ALERT;
    }
    else if (d.plan.required)
    {
        out->action = SB_ACTION_MOVE;
        out->burn_gps_ms = d.burn_epoch_ms;
        out->dv_rtn_mps[0] = d.plan.dv_hill.x;
        out->dv_rtn_mps[1] = d.plan.dv_hill.y;
        out->dv_rtn_mps[2] = d.plan.dv_hill.z;
        out->dv_mps = d.plan.dv_mag;
        out->return_dv_mps = d.ret.total_dv;
    }
    else
    {
        /* Give-way, but already outside the safety margin: nothing to do. */
        out->action = SB_ACTION_NOTHING;
    }

    s.p = out->summary;
    s.cap = (uint32_t)sizeof(out->summary);
    s.n = 0U;
    switch (out->action)
    {
        case SB_ACTION_MOVE:
            sb_put(&s, "MOVE: give way to ");
            break;
        case SB_ACTION_HOLD:
            sb_put(&s, "HOLD COURSE: ");
            break;
        case SB_ACTION_ALERT:
            sb_put(&s, "ALERT OPERATORS: you should give way to ");
            break;
        default:
            sb_put(&s, "NO ACTION: low risk from ");
            break;
    }
    sb_put_n(&s, out->other_id, 12U);
    if (out->other_name[0] != '\0')
    {
        sb_put(&s, " (");
        sb_put(&s, out->other_name);
        sb_put(&s, ")");
    }
    if (out->action == SB_ACTION_HOLD)
    {
        sb_put(&s, " gives way; keep your planned trajectory");
    }
    else if (out->action == SB_ACTION_ALERT)
    {
        sb_put(&s, " but cannot maneuver");
    }
    else if (out->action == SB_ACTION_MOVE)
    {
        sb_put(&s, ". Burn ");
        sb_put_fixed(&s, out->dv_mps, 3U, false);
        sb_put(&s, " m/s ");
        sb_put_lead(&s, out->tca_gps_ms - out->burn_gps_ms);
        sb_put(&s, " before closest approach (RTN ");
        sb_put_fixed(&s, out->dv_rtn_mps[0], 3U, true);
        sb_put(&s, ", ");
        sb_put_fixed(&s, out->dv_rtn_mps[1], 3U, true);
        sb_put(&s, ", ");
        sb_put_fixed(&s, out->dv_rtn_mps[2], 3U, true);
        sb_put(&s, " m/s)");
        if (out->late != 0)
        {
            sb_put(&s, " [late]");
        }
    }
    else
    {
        /* NOTHING */
    }
    sb_put(&s, ". Closest approach ");
    sb_put_fixed(&s, out->miss_m, 0U, false);
    sb_put(&s, " m, Pc ");
    sb_put_sci(&s, out->pc);
    if (out->rule != 0)
    {
        sb_put(&s, "; decided by the ");
        sb_put(&s, out->rule_text);
    }
    if (out->advisory != 0)
    {
        sb_put(&s, "; ADVISORY, operators should confirm");
    }
    sb_put(&s, ".");
    return out->action;
}

uint32_t sb_easy_selftest(void)
{
    static SbFsw ctx;
    SbFswConfig cfg;
    sb_easy_profile_config(SB_SAT_SMALLSAT, &cfg);
    cfg.self_id = sb_make_object_id("SELFTEST");
    (void)sb_fsw_init(&ctx, &cfg);
    return ctx.selftest_failed;
}

uint32_t sb_easy_advice_size(void)
{
    return (uint32_t)sizeof(SbAdvice);
}

const char *sb_easy_action_str(SbAction a)
{
    switch (a)
    {
        case SB_ACTION_NOTHING: return "NOTHING";
        case SB_ACTION_MOVE:    return "MOVE";
        case SB_ACTION_HOLD:    return "HOLD";
        case SB_ACTION_ALERT:   return "ALERT";
        default:                return "ERROR";
    }
}

const char *sb_easy_profile_str(SbSatProfile p)
{
    switch (p)
    {
        case SB_SAT_NO_PROPULSION: return "no-propulsion";
        case SB_SAT_CUBESAT:       return "cubesat";
        case SB_SAT_SMALLSAT:      return "smallsat";
        case SB_SAT_CONSTELLATION: return "constellation";
        case SB_SAT_HIGH_VALUE:    return "high-value";
        case SB_SAT_CREWED:        return "crewed";
        default:                   return "unknown";
    }
}
