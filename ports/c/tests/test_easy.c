// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — tests for the five-minute API (starboard_easy.h).
// Copyright (C) 2026 James H. All rights reserved.
//
// The easy layer must make NO decision of its own: for every example CDM,
// both perspectives and every profile, its answer is checked against the
// flight layer's sb_fsw_decide called directly. Plus identity matching,
// fail-safe errors and the plain-English summary.
//
//   test_easy EXAMPLES_DIR
#include "../starboard_easy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (cond) { ++g_pass; }                             \
        else { ++g_fail; printf("FAIL %s:%d: ", __FILE__,   \
               __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } while (0)

static char *slurp(const char *dir, const char *name, uint32_t *len)
{
    char path[512];
    FILE *f;
    long n;
    char *b;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (f == NULL) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1U);
    if (fread(b, 1U, (size_t)n, f) != (size_t)n) { exit(2); }
    fclose(f);
    b[n] = '\0';
    *len = (uint32_t)n;
    return b;
}

/* Re-derive the answer through the flight layer, independently. */
static void reference(const char *text, uint32_t len, uint32_t si,
                      SbSatProfile p, SbDecision *d, bool *can)
{
    SbCdm cdm;
    SbFswConfig cfg;
    SbCounterpart other;
    uint32_t line;
    if (sb_cdm_parse(text, len, &cdm, &line) != SB_CDM_OK) { exit(3); }
    sb_easy_profile_config(p, &cfg);
    cfg.self_id = cdm.obj[si].id;
    memset(&other, 0, sizeof(other));
    if (sb_fsw_decide(&cfg, &cdm, &other, cdm.tca_gps_ms - 21600000, d)
        != SB_FSW_OK) { exit(3); }
    *can = cfg.self_can_maneuver;
}

static SbAction expected_action(const SbDecision *d, bool can)
{
    if (d->role == SB_ROLE_NONE) return SB_ACTION_NOTHING;
    if (d->role == SB_ROLE_STAND_ON) return SB_ACTION_HOLD;
    if (!can) return SB_ACTION_ALERT;
    return d->plan.required ? SB_ACTION_MOVE : SB_ACTION_NOTHING;
}

int main(int argc, char **argv)
{
    static const char *const files[3] = {
        "crossing_demo.cdm", "shell_vs_debris.cdm", "itrf_rocket_body.cdm"
    };
    const char *dir = (argc > 1) ? argv[1] : "../../examples/cdm";
    uint32_t fi;
    uint32_t len;
    char *text;
    SbAdvice a;
    SbAdvice b;
    int p;

    CHECK(sb_easy_selftest() == 0U, "self-test failed on this build");

    for (fi = 0U; fi < 3U; ++fi)
    {
        SbCdm cdm;
        uint32_t line;
        text = slurp(dir, files[fi], &len);
        sb_cdm_parse(text, len, &cdm, &line);
        for (p = 0; p < (int)SB_SAT_PROFILE_COUNT; ++p)
        {
            uint32_t si;
            for (si = 0U; si < 2U; ++si)
            {
                SbDecision d;
                bool can;
                char id[13];
                memcpy(id, cdm.obj[si].id.c, 12U);
                id[12] = '\0';
                reference(text, len, si, (SbSatProfile)p, &d, &can);
                sb_easy_decide(text, len, id, (SbSatProfile)p, 0, &a);
                CHECK(a.action == expected_action(&d, can),
                      "%s side %u profile %s: action %s", files[fi], si,
                      sb_easy_profile_str((SbSatProfile)p),
                      sb_easy_action_str(a.action));
                CHECK(a.conjunction_id == d.conjunction_id, "conj id");
                CHECK(memcmp(&a.pc, &d.pc, sizeof(double)) == 0, "pc bits");
                CHECK(memcmp(&a.miss_m, &d.miss_m, sizeof(double)) == 0,
                      "miss bits");
                CHECK(a.rule == (int)d.rule, "rule");
                if (a.action == SB_ACTION_MOVE)
                {
                    /* The window search is never worse than the preferred
                     * lead, and the chosen burn is exactly the flight
                     * layer's plan for that lead. */
                    SbCdm c2;
                    SbFswConfig cfg2;
                    SbCounterpart o2;
                    SbDecision d2;
                    uint32_t l2;
                    CHECK(a.dv_mps <= d.plan.dv_mag, "search worse: %f > %f",
                          a.dv_mps, d.plan.dv_mag);
                    sb_cdm_parse(text, len, &c2, &l2);
                    sb_easy_profile_config((SbSatProfile)p, &cfg2);
                    cfg2.self_id = c2.obj[si].id;
                    cfg2.burn_lead_ms = a.tca_gps_ms - a.burn_gps_ms;
                    CHECK(cfg2.burn_lead_ms % 300000 == 0 &&
                          cfg2.burn_lead_ms >= 1200000 &&
                          cfg2.burn_lead_ms <= 16200000, "lead on grid");
                    memset(&o2, 0, sizeof(o2));
                    sb_fsw_decide(&cfg2, &c2, &o2, c2.tca_gps_ms - 21600000,
                                  &d2);
                    CHECK(memcmp(&a.dv_rtn_mps[1], &d2.plan.dv_hill.y,
                                 sizeof(double)) == 0 &&
                          a.burn_gps_ms == d2.burn_epoch_ms &&
                          d2.late == 0U, "burn = flight plan for its lead");
                    CHECK(strstr(a.summary, "MOVE: give way to") != NULL,
                          "summary: %s", a.summary);
                    CHECK(strstr(a.summary, " m/s ") != NULL, "summary dv");
                }
                CHECK(strlen(a.summary) > 20U, "summary empty");
                CHECK(strcmp(a.self_id, id) == 0, "self id");
            }
        }
        /* Identity: designator, catalog number and name all select a side;
         * NULL means OBJECT1. */
        sb_easy_decide(text, len, cdm.obj[1].designator, SB_SAT_SMALLSAT, 0,
                       &a);
        sb_easy_decide(text, len, cdm.obj[1].name, SB_SAT_SMALLSAT, 0, &b);
        CHECK(a.action != SB_ACTION_ERROR && strcmp(a.self_id, b.self_id) == 0,
              "catalog number vs name");
        sb_easy_decide(text, len, NULL, SB_SAT_SMALLSAT, 0, &b);
        CHECK(memcmp(b.self_id, cdm.obj[0].id.c, 12U) == 0, "NULL = OBJECT1");
        /* Determinism: same call, same bytes. */
        sb_easy_decide(text, len, NULL, SB_SAT_CONSTELLATION, 0, &a);
        sb_easy_decide(text, len, NULL, SB_SAT_CONSTELLATION, 0, &b);
        CHECK(memcmp(&a.summary, &b.summary, sizeof(a.summary)) == 0 &&
              a.conjunction_id == b.conjunction_id, "deterministic");
        /* Not involved: fail-safe error, never NOTHING. */
        CHECK(sb_easy_decide(text, len, "1999-999Z", SB_SAT_SMALLSAT, 0, &a)
              == SB_ACTION_ERROR, "unknown self must be an error");
        CHECK(strstr(a.summary, "not as safe") != NULL, "error summary");
        /* Truncated warning: fail-safe error with a reason. */
        CHECK(sb_easy_decide(text, len / 2U, NULL, SB_SAT_SMALLSAT, 0, &a)
              == SB_ACTION_ERROR && a.error_text != NULL,
              "truncated CDM must be an error");
        free(text);
    }

    /* Known answers for the shipped examples (both perspectives). */
    text = slurp(dir, "shell_vs_debris.cdm", &len);
    CHECK(sb_easy_decide(text, len, "SHELL-1042", SB_SAT_CONSTELLATION, 0, &a)
          == SB_ACTION_MOVE, "shell sat moves for debris: %s", a.summary);
    CHECK(sb_easy_decide(text, len, "SHELL-1042", SB_SAT_NO_PROPULSION, 0, &a)
          == SB_ACTION_ALERT, "no propulsion -> alert: %s", a.summary);
    free(text);
    text = slurp(dir, "itrf_rocket_body.cdm", &len);
    CHECK(sb_easy_decide(text, len, "99021", SB_SAT_NO_PROPULSION, 0, &a)
          == SB_ACTION_ALERT, "rocket body cannot move: %s", a.summary);
    free(text);

    /* Bad arguments. */
    CHECK(sb_easy_decide(NULL, 0U, NULL, SB_SAT_SMALLSAT, 0, &a)
          == SB_ACTION_ERROR, "NULL cdm");
    CHECK(sb_easy_decide("x", 1U, NULL, (SbSatProfile)99, 0, &a)
          == SB_ACTION_ERROR, "bad profile");
    CHECK(sb_easy_decide("x", 1U, NULL, SB_SAT_SMALLSAT, 0, NULL)
          == SB_ACTION_ERROR, "NULL out");

    printf("test_easy: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
