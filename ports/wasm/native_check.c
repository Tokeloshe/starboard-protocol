/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Runs the browser shim natively over every example x side x profile and
 * prints one JSON line each; tools/check_wasm.js prints the same from the
 * .wasm. CI diffs the two: the browser must agree with native C exactly. */
#include "starboard_wasm.c"
#include <string.h>

int main(int argc, char **argv)
{
    int f;
    for (f = 1; f < argc; ++f)
    {
        FILE *fp = fopen(argv[f], "rb");
        size_t n;
        SbCdm cdm;
        uint32_t line;
        int side, p;
        if (fp == NULL) { return 2; }
        n = fread(sbw_in, 1U, SBW_IN_MAX, fp);
        fclose(fp);
        if (sb_cdm_parse(sbw_in, (uint32_t)n, &cdm, &line) != SB_CDM_OK) { return 2; }
        for (side = 0; side < 2; ++side)
        {
            for (p = 0; p < (int)SB_SAT_PROFILE_COUNT; ++p)
            {
                memcpy(sbw_me, cdm.obj[side].id.c, 12U);
                sbw_me[12] = '\0';
                sbw_decide((unsigned)n, p, 0);
                printf("%s\n", sbw_out);
            }
        }
    }
    printf("selftest %u\n", sbw_selftest());
    return 0;
}
