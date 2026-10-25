#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — fuzz campaign (libFuzzer + ASan + UBSan).
# Copyright (C) 2026 James H. All rights reserved.
#
#   verification/fuzz/run.sh [SECONDS_PER_TARGET]     (default 60)
# Crashes, leaks, UB, timeouts (>10 s per input) and DIFFERENTIAL mismatches
# (C vs C++ bindings disagreeing) all fail the run.
set -eu
cd "$(dirname "$0")"
ROOT=../..
C=$ROOT/ports/c
T=${1:-60}
OUT=${FUZZ_OUT:-$(mktemp -d)}
SAN="-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -g -O1"
LIB="$C/canon.c $C/geometry.c $C/propagator.c $C/covariance.c
     $C/screening.c $C/rules.c $C/maneuver.c $C/multibody.c $C/broadphase.c
     $C/sha512.c $C/registry.c $C/ed25519.c $C/cdm.c $C/fsw.c $C/ccsds.c
     $C/flock.c"
mkdir -p "$OUT/obj" "$OUT/seed_cdm" "$OUT/seed_dec" "$OUT/seed_notice"
cp $ROOT/examples/cdm/*.cdm regressions/* "$OUT/seed_cdm/"
printf '\0031234.5678' > "$OUT/seed_dec/a"; printf '\003-1.25E+03' > "$OUT/seed_dec/b"
for f in $LIB; do
    clang -std=c99 -ffp-contract=off $SAN -c "$f" -I$C \
        -o "$OUT/obj/$(basename "$f" .c).o" 2>/dev/null
done
OBJS="$OUT"/obj/*.o
build_c()   { clang -std=c99 -ffp-contract=off $SAN -I$C "$1" $OBJS -lm -o "$2"; }
build_cpp() { clang++ -std=c++17 -ffp-contract=off $SAN -I$C -I$ROOT/core/include "$1" $OBJS -lm -o "$2"; }
build_cpp fuzz_cdm_diff.cpp     "$OUT/fuzz_cdm_diff"
build_cpp fuzz_decimal_diff.cpp "$OUT/fuzz_decimal_diff"
build_c   fuzz_flock.c          "$OUT/fuzz_flock"
build_c   fuzz_fsw.c            "$OUT/fuzz_fsw"
# A valid notice as flock seed, from the murmuration encoder.
clang -std=c99 -ffp-contract=off -I$C -x c - -x none $OBJS -lm -fsanitize=address,undefined \
    -o "$OUT/mknotice" <<'CEOF'
#include "starboard_flock.h"
#include <stdio.h>
#include <string.h>
int main(void){ SbNotice n; uint8_t w[SB_NOTICE_BYTES]; memset(&n,0,sizeof n);
 n.hops=4; n.origin=sb_make_object_id("2026-300A"); n.seq=1; n.post_burn.position.x=6.9e6;
 n.post_burn.velocity.y=7600; n.burn_epoch_ms=1; n.mirror_epoch_ms=5700001; n.final_epoch_ms=17100001;
 sb_notice_encode(&n,w); fwrite(w,1,sizeof w,stdout); return 0;}
CEOF
"$OUT/mknotice" > "$OUT/seed_notice/n1"
status=0
for t in fuzz_cdm_diff:seed_cdm fuzz_fsw:seed_cdm fuzz_decimal_diff:seed_dec fuzz_flock:seed_notice; do
    bin=${t%%:*}; seed=${t##*:}
    mkdir -p "$OUT/corpus_$bin"
    if "$OUT/$bin" "$OUT/corpus_$bin" "$OUT/$seed" -max_total_time="$T" \
         -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 \
         > "$OUT/$bin.log" 2>&1; then
        runs=$(grep -m1 "stat::number_of_executed_units" "$OUT/$bin.log" | awk '{print $2}')
        cov=$(grep -m1 "stat::peak_rss_mb" "$OUT/$bin.log" | awk '{print $2}')
        echo "OK    $bin: ${runs:-?} executions in ${T}s, no crash/UB/leak/mismatch (peak RSS ${cov:-?} MB)"
    else
        echo "FAIL  $bin (see $OUT/$bin.log)"; tail -30 "$OUT/$bin.log"; status=1
    fi
done
exit $status
