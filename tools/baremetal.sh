#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — bare-metal Cortex-M campaign (no operating system).
# Copyright (C) 2026 James H. All rights reserved.
#
# Cross-compiles the C99 flight library with arm-none-eabi-gcc + newlib for
# three microcontroller classes and runs, on QEMU's MPS2 boards over ARM
# semihosting: the v1 transcript, the v2 transcript, and a flight image (boot
# self-test, one CDM decision, CCSDS telemetry). Also reports the flight
# library's own flash/RAM footprint and worst-case stack depth.
#
#   tools/baremetal.sh [--markdown]
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
MD=0; [ "${1:-}" = "--markdown" ] && MD=1
C=ports/c
LIB="$C/canon.c $C/geometry.c $C/propagator.c $C/covariance.c $C/screening.c
     $C/rules.c $C/maneuver.c $C/multibody.c $C/broadphase.c $C/sha512.c
     $C/registry.c $C/ed25519.c $C/cdm.c $C/fsw.c $C/ccsds.c $C/flock.c
     $C/softdouble.c"
COMMON="-std=c99 -O2 -ffp-contract=off -ffunction-sections -fdata-sections"
LINK="--specs=rdimon.specs -T $C/baremetal/mps2.ld -Wl,--gc-sections -lm"

# name | board | cpu flags | doubles
BOARDS=(
"Cortex-M7 (FPv5 double FPU)|mps2-an500|-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard|hardware"
"Cortex-M4F (single-precision FPU)|mps2-an386|-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard|software"
"Cortex-M3 (no FPU)|mps2-an385|-mcpu=cortex-m3 -mthumb -mfloat-abi=soft|software"
"FINDING: Cortex-M4F, stock libgcc doubles|mps2-an386|-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DSB_NO_SOFTDOUBLE|software (libgcc)"
)

command -v arm-none-eabi-gcc >/dev/null || { echo "need arm-none-eabi-gcc"; exit 2; }
command -v qemu-system-arm >/dev/null || { echo "need qemu-system-arm"; exit 2; }

ROWS=()
fail=0
for b in "${BOARDS[@]}"; do
    IFS='|' read -r name board cpu dbl <<< "$b"
    d="$W/$board"; mkdir -p "$d"
    ok=1
    for prog in main ext_bits selftest; do
        src=$C/$prog.c; [ $prog = selftest ] && src=$C/baremetal/selftest_main.c
        if ! arm-none-eabi-gcc $cpu $COMMON -I$C $C/baremetal/startup.c $src \
                $LIB $LINK -o "$d/$prog.elf" > "$d/$prog.build" 2>&1; then
            echo "FAIL  $name: build $prog"; head "$d/$prog.build"; ok=0
        fi
    done
    [ $ok = 1 ] || { fail=1; continue; }
    q="qemu-system-arm -M $board -nographic -semihosting-config enable=on,target=native -kernel"
    timeout 900 $q "$d/main.elf" > "$d/v1.txt" 2>/dev/null
    timeout 900 $q "$d/ext_bits.elf" > "$d/v2.txt" 2>/dev/null
    timeout 300 $q "$d/selftest.elf" > "$d/st.txt" 2>/dev/null; strc=$?
    v1=diverges; v2=diverges; st=FAIL
    diff -q conformance/expected_bits.txt "$d/v1.txt" >/dev/null && v1=identical
    diff -q conformance/expected_bits_ext.txt "$d/v2.txt" >/dev/null && v2=identical
    grep -q "selftest mask 0x00000000" "$d/st.txt" && [ $strc = 0 ] && st=pass
    if [ $v1 = identical ] && [ $v2 = identical ] && [ $st = pass ]; then
        verdict="CONFORMANT"
    elif [ $st = FAIL ] && grep -q "mode 1" "$d/st.txt" && \
         [ "${name#FINDING}" != "$name" ]; then
        verdict="NON-CONFORMANT, refused by self-test (SAFE)"
    else
        verdict="UNSAFE: bad bits not caught"; fail=1
    fi
    echo "$name [$board, $dbl doubles]: v1 $v1, v2 $v2, self-test $st -> $verdict"
    sed 's/^/      /' "$d/st.txt" | head -6
    ROWS+=("| $name | $dbl | $v1 | $v2 | $st | $verdict |")
done

# Flight-library footprint: the library objects alone (no newlib printf,
# no test harness), measured as the sum of the sections the linker keeps
# when a minimal flight image references the full public API.
echo
echo "Flight library footprint (Cortex-M7, -O2, --gc-sections, no printf):"
cat > "$W/foot.c" <<'EOF'
#include "starboard_fsw.h"
static SbFsw ctx;
static SbFlock flock;
static uint8_t pkt[SB_FSW_TM_MAX_BYTES];
static uint8_t relay[SB_NOTICE_BYTES];
extern const char *volatile g_text;
const char *volatile g_text = "";
int main(void)
{
    SbFswConfig cfg = {0};
    uint32_t rl = 0u;
    cfg.operating_mode = SB_MODE_AUTONOMOUS;
    (void)sb_fsw_init(&ctx, &cfg);
    (void)sb_fsw_ingest_cdm(&ctx, g_text, 0u, 0, 0);
    (void)sb_fsw_set_counterpart(&ctx, cfg.self_id, TRUST_VERIFIED,
                                 TIER_OPERATIONAL, true, 0);
    (void)sb_fsw_poll_tm(&ctx, pkt, sizeof(pkt), 0);
    (void)sb_fsw_notice(&ctx, 0u, relay);
    sb_flock_init(&flock, cfg.self_id);
    (void)sb_flock_receive(&flock, relay, SB_NOTICE_BYTES, relay, &rl);
    (void)sb_flock_check(&flock.e[0].n, flock.e[0].n.post_burn, 0);
    return 0;
}
EOF
cpu7="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard"
arm-none-eabi-gcc $cpu7 $COMMON -I$C -fstack-usage -fcallgraph-info=su \
    -c "$W/foot.c" -o "$W/foot.o" 2>/dev/null
for f in $LIB; do
    arm-none-eabi-gcc $cpu7 $COMMON -fstack-usage -fcallgraph-info=su \
        -c "$f" -o "$W/$(basename "$f" .c).o" 2>/dev/null
done
arm-none-eabi-gcc $cpu7 --specs=nosys.specs --specs=nano.specs -nostartfiles \
    -Wl,--gc-sections -Wl,-e,main "$W"/*.o -lm -o "$W/foot.elf" 2>/dev/null
read -r text data bss _ < <(arm-none-eabi-size "$W/foot.elf" | tail -1)
echo "  code+rodata ${text} B, data ${data} B, bss ${bss} B (SbFsw context: incl.)"
python3 "$ROOT/tools/stack_depth.py" "$W" main 2>/dev/null | sed 's/^/  /'
if [ $MD = 1 ]; then
    echo
    echo "| MCU class | doubles | v1 transcript | v2 transcript | self-test | verdict |"
    echo "|---|---|---|---|---|---|"
    for r in "${ROWS[@]}"; do echo "$r"; done
fi
exit $fail
