#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — cross-ARCHITECTURE bit-agreement campaign.
# Copyright (C) 2026 James H. All rights reserved.
#
# Builds the C99 flight library for flight-relevant CPU families with real
# cross-compilers and runs `make check` under QEMU user-mode emulation:
# the normative v1 transcript, the extended v2 transcript (maneuvers,
# windows, broad-phase, CDM binding), the built-in self-test and the flight /
# flock test suites. Every conformant target must reproduce the SAME bytes.
#
# Two deliberately NON-conformant controls prove the boot self-test is not
# decorative: an x87 build (excess precision) and an FMA-contracting build.
# Both must produce wrong bits AND be caught by the self-test (SAFE mode).
#
#   tools/crossarch.sh              # all targets whose toolchain is present
#   tools/crossarch.sh --markdown   # also print a results table
#
# Debian/Ubuntu: apt install qemu-user gcc-{aarch64,arm,powerpc,...}-linux-gnu
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
MD=0
[ "${1:-}" = "--markdown" ] && MD=1

# name | CC | extra CFLAGS | runner | class
TARGETS=(
"x86-64|gcc||(native)|Starlink-class Linux SBC / ground"
"x86 32-bit SSE2|i686-linux-gnu-gcc|-msse2 -mfpmath=sse|qemu-i386 -L /usr/i686-linux-gnu|legacy 32-bit x86"
"AArch64 (ARMv8-A)|aarch64-linux-gnu-gcc||qemu-aarch64 -L /usr/aarch64-linux-gnu|Zynq UltraScale+ / Cortex-A53/A72 OBCs"
"ARMv7-A hard-float|arm-linux-gnueabihf-gcc||qemu-arm -L /usr/arm-linux-gnueabihf|Zynq-7000 CubeSat OBCs"
"PowerPC 32 big-endian|powerpc-linux-gnu-gcc|-mcpu=750|qemu-ppc -cpu 750 -L /usr/powerpc-linux-gnu|RAD750-class (PowerPC 750)"
"PowerPC64 LE|powerpc64le-linux-gnu-gcc||qemu-ppc64le -L /usr/powerpc64le-linux-gnu|POWER-class ground segment"
"ARMv5 soft-float|arm-linux-gnueabi-gcc|-march=armv5te -mfloat-abi=soft|qemu-arm -L /usr/arm-linux-gnueabi|FPU-less ARM, with Starboard softdouble.c"
"RISC-V 64|riscv64-linux-gnu-gcc||qemu-riscv64 -L /usr/riscv64-linux-gnu|NASA HPSC / NOEL-V class"
"SPARC64 big-endian|sparc64-linux-gnu-gcc||qemu-sparc64 -L /usr/sparc64-linux-gnu|SPARC (LEON heritage ISA)"
"MIPS32 big-endian|mips-linux-gnu-gcc||qemu-mips -L /usr/mips-linux-gnu|MIPS (legacy avionics)"
"s390x big-endian|s390x-linux-gnu-gcc||qemu-s390x -L /usr/s390x-linux-gnu|z/Architecture (IBM IEEE BFP)"
)
# FINDINGS: platforms whose arithmetic is NOT IEEE-754 conformant. They
# must produce wrong bits AND be refused by the self-test, like the controls.
#  * ARM soft-float (libgcc __aeabi_dadd as executed under QEMU): a double
#    subtraction with an exponent gap of exactly 33 is misrounded by one ulp
#    (897 of 200,000 random cases, every one at gap 33; adjudicated with exact
#    rational arithmetic). Consistent with the >= 32-bit alignment path of
#    libgcc's ieee754-df.S; ports/c/softdouble.c replaces the routine and the
#    same target then conforms (row "ARMv5 soft-float" above). Silicon
#    confirmation open; see docs/evidence/CROSS-ARCH.md.
CONTROLS=(
"FINDING: ARMv5 soft-float, stock libgcc|arm-linux-gnueabi-gcc|-march=armv5te -mfloat-abi=soft -DSB_NO_SOFTDOUBLE|qemu-arm -L /usr/arm-linux-gnueabi|libgcc __aeabi_dadd misrounds at exponent gap 33"
"CONTROL: x87 excess precision|i686-linux-gnu-gcc|-mfpmath=387|qemu-i386 -L /usr/i686-linux-gnu|must FAIL bits and self-test"
"CONTROL: FMA contraction on|aarch64-linux-gnu-gcc|-ffp-contract=fast|qemu-aarch64 -L /usr/aarch64-linux-gnu|must FAIL bits and self-test"
)

stage() {       # fresh copy of the tree the Makefile expects
    local d=$1
    mkdir -p "$d/ports" "$d/conformance" "$d/examples"
    cp -r "$ROOT/ports/c" "$d/ports/c"
    cp "$ROOT"/conformance/expected_bits*.txt "$ROOT"/conformance/ext_cdm_gen.h \
       "$d/conformance/"
    cp -r "$ROOT/examples/cdm" "$ROOT/examples/murmuration.c" "$d/examples/"
    make -s -C "$d/ports/c" clean >/dev/null 2>&1
}

pass=0; fail=0; skip=0
ROWS=()
run_one() {
    local spec=$1 expect_fail=$2
    IFS='|' read -r name cc extra runner class <<< "$spec"
    if ! command -v "$cc" >/dev/null 2>&1; then
        echo "SKIP  $name  ($cc not installed)"; skip=$((skip+1)); return
    fi
    local qemu=${runner%% *}
    if [ "$runner" != "(native)" ] && ! command -v "$qemu" >/dev/null 2>&1; then
        echo "SKIP  $name  ($qemu not installed)"; skip=$((skip+1)); return
    fi
    local d="$WORK/$(echo "$name" | tr -c 'a-zA-Z0-9' _)"
    stage "$d"
    local run=""; [ "$runner" != "(native)" ] && run=$runner
    local flags="-std=c99 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic $extra"
    # The FMA control must override the contraction flag, not add to it.
    case "$extra" in *fp-contract=fast*) flags="-std=c99 -O2 $extra";; esac
    if ! make -s -C "$d/ports/c" CC="$cc" CFLAGS="$flags" \
            all > "$d/build.log" 2>&1; then
        echo "FAIL  $name  (build)"; sed 's/^/      /' "$d/build.log" | head -20
        fail=$((fail+1)); ROWS+=("| $name | $class | build failed |"); return
    fi
    ( cd "$d/ports/c" && $run ./sb_bits > v1.txt 2>/dev/null; \
      $run ./sb_ext > v2.txt 2>/dev/null; \
      $run ./sbctl selftest > st.txt 2>&1; echo $? > st.rc )
    local v1=FAIL v2=FAIL st=FAIL tests=n/a
    diff -q "$ROOT/conformance/expected_bits.txt" "$d/ports/c/v1.txt" >/dev/null && v1=ok
    diff -q "$ROOT/conformance/expected_bits_ext.txt" "$d/ports/c/v2.txt" >/dev/null && v2=ok
    [ "$(cat "$d/ports/c/st.rc")" = 0 ] && st=ok
    if [ "$expect_fail" = 0 ]; then
        if ( cd "$d/ports/c" && $run ./test_fsw ../../examples/cdm && \
             $run ./test_fsw_fi ../../examples/cdm && $run ./test_flock ) \
             > "$d/tests.log" 2>&1; then tests=ok; else tests=FAIL; fi
        if [ $v1 = ok ] && [ $v2 = ok ] && [ $st = ok ] && [ $tests = ok ]; then
            echo "OK    $name   v1 transcript, v2 transcript, self-test, 102 tests"
            pass=$((pass+1))
            ROWS+=("| $name | $class | ✅ identical | ✅ identical | ✅ pass | ✅ pass |")
        else
            echo "FAIL  $name   v1=$v1 v2=$v2 selftest=$st tests=$tests"
            diff "$ROOT/conformance/expected_bits.txt" "$d/ports/c/v1.txt" | sed 's/^/      /'
            fail=$((fail+1))
            ROWS+=("| $name | $class | $v1 | $v2 | $st | $tests |")
        fi
    else
        local caught=no
        grep -q "SAFE" "$d/ports/c/st.txt" && caught=yes
        if [ $caught = yes ] && { [ $v1 = FAIL ] || [ $v2 = FAIL ]; }; then
            echo "OK    $name   bits diverge (v1=$v1 v2=$v2) AND self-test" \
                 "refuses: $(grep -c FAIL "$d/ports/c/st.txt") check(s) failed -> SAFE"
            pass=$((pass+1))
            ROWS+=("| $name | $class | ❌ diverges | ❌ diverges | 🛑 caught → SAFE | — |")
        else
            echo "FAIL  $name   control not caught (v1=$v1 v2=$v2 caught=$caught)"
            fail=$((fail+1))
            ROWS+=("| $name | $class | $v1 | $v2 | NOT caught | — |")
        fi
    fi
}

for t in "${TARGETS[@]}"; do run_one "$t" 0; done
for t in "${CONTROLS[@]}"; do run_one "$t" 1; done

echo "crossarch: $pass ok, $fail failed, $skip skipped"
if [ $MD = 1 ]; then
    echo
    echo "| Target | Flight class | v1 transcript | v2 transcript | Self-test | Flight + flock tests |"
    echo "|---|---|---|---|---|---|"
    for r in "${ROWS[@]}"; do echo "$r"; done
fi
[ $fail -eq 0 ]
