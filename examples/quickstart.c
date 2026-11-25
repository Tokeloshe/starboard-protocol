// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — the five-minute quickstart.
//
/*
 * Build (from the repository root, with the single-file library):
 *     cc -std=c99 -O2 -ffp-contract=off -Idist examples/quickstart.c
 *        dist/starboard.c -lm -o quickstart
 *     ./quickstart examples/cdm/shell_vs_debris.cdm SHELL-1042 constellation
 *
 * Arguments: CDM file, which object is you (designator, catalog number or
 * name), and your profile. It prints what your satellite should do.
 */
#include "starboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static char cdm[1 << 20];
    SbSatProfile profile = SB_SAT_SMALLSAT;
    SbAdvice advice;
    FILE *f;
    size_t len;
    int p;

    if (argc < 2)
    {
        fprintf(stderr, "usage: quickstart CDM_FILE [ME] [PROFILE]\n"
                        "profiles: no-propulsion cubesat smallsat "
                        "constellation high-value crewed\n");
        return 2;
    }
    for (p = 0; (argc > 3) && (p < (int)SB_SAT_PROFILE_COUNT); ++p)
    {
        if (strcmp(argv[3], sb_easy_profile_str((SbSatProfile)p)) == 0)
        {
            profile = (SbSatProfile)p;
        }
    }

    /* 1. Make sure this build computes exactly like every other satellite. */
    if (sb_easy_selftest() != 0U)
    {
        fprintf(stderr, "self-test FAILED: rebuild with -ffp-contract=off\n");
        return 1;
    }

    /* 2. Read the collision warning (a CCSDS CDM). */
    f = fopen(argv[1], "rb");
    if (f == NULL)
    {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    len = fread(cdm, 1U, sizeof(cdm), f);
    fclose(f);

    /* 3. Decide. */
    sb_easy_decide(cdm, (uint32_t)len, (argc > 2) ? argv[2] : NULL, profile, 0,
                   &advice);
    printf("%s\n", advice.summary);

    if (advice.action == SB_ACTION_MOVE)
    {
        /* Hand this to your attitude & orbit control: */
        printf("  burn at GPS ms %lld, dv RTN = [%.4f, %.4f, %.4f] m/s\n",
               (long long)advice.burn_gps_ms, advice.dv_rtn_mps[0],
               advice.dv_rtn_mps[1], advice.dv_rtn_mps[2]);
    }
    return (advice.action == SB_ACTION_ERROR) ? 1 : 0;
}
