/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Starboard Protocol — WebAssembly shim for the browser (site/try.html).
 * The browser writes a CDM into sbw_input(), calls sbw_decide(), and reads
 * a JSON answer from sbw_output(). The decision is the C flight code's own
 * (dist/starboard.c compiled to wasm32), so it is bit-identical to what a
 * spacecraft computes; sbw_selftest() proves that on the visitor's machine.
 *
 *   tools/build_wasm.sh   ->   site/starboard.wasm
 */
#include "starboard.h"
#include <stdio.h>

#define SBW_IN_MAX  (65536)
#define SBW_OUT_MAX (4096)

static char sbw_in[SBW_IN_MAX];
static char sbw_me[64];
static char sbw_out[SBW_OUT_MAX];

char *sbw_input(void) { return sbw_in; }
char *sbw_me_buf(void) { return sbw_me; }
char *sbw_output(void) { return sbw_out; }
unsigned sbw_input_cap(void) { return SBW_IN_MAX; }

static void esc(char *dst, size_t cap, const char *s)
{
    size_t n = 0;
    while (*s != '\0' && n + 2 < cap)
    {
        if (*s == '"' || *s == '\\') { dst[n++] = '\\'; }
        dst[n++] = (*s >= ' ') ? *s : ' ';
        ++s;
    }
    dst[n] = '\0';
}

unsigned sbw_selftest(void) { return sb_easy_selftest(); }

/* Returns the JSON length written to sbw_output(). */
int sbw_decide(unsigned len, int profile, long long now_gps_ms)
{
    SbAdvice a;
    char summary[512], name[96], err[128];
    sb_easy_decide(sbw_in, len, (sbw_me[0] != '\0') ? sbw_me : NULL,
                   (SbSatProfile)profile, (int64_t)now_gps_ms, &a);
    esc(summary, sizeof summary, a.summary);
    esc(name, sizeof name, a.other_name);
    esc(err, sizeof err, a.error_text ? a.error_text : "");
    return snprintf(sbw_out, SBW_OUT_MAX,
        "{\"action\":\"%s\",\"summary\":\"%s\",\"self_id\":\"%s\","
        "\"other_id\":\"%s\",\"other_name\":\"%s\","
        "\"conjunction_id\":\"%016llX\",\"miss_m\":%.17g,\"pc\":%.17g,"
        "\"tca_gps_ms\":%lld,\"rule\":%d,\"rule_text\":\"%s\","
        "\"advisory\":%s,\"burn_gps_ms\":%lld,\"dv_rtn_mps\":[%.17g,%.17g,%.17g],"
        "\"dv_mps\":%.17g,\"return_dv_mps\":%.17g,\"late\":%s,"
        "\"error\":\"%s\",\"error_line\":%u}",
        sb_easy_action_str(a.action), summary, a.self_id, a.other_id, name,
        (unsigned long long)a.conjunction_id, a.miss_m, a.pc,
        (long long)a.tca_gps_ms, a.rule, a.rule_text ? a.rule_text : "",
        a.advisory ? "true" : "false", (long long)a.burn_gps_ms,
        a.dv_rtn_mps[0], a.dv_rtn_mps[1], a.dv_rtn_mps[2], a.dv_mps,
        a.return_dv_mps, a.late ? "true" : "false", err, a.error_line);
}
