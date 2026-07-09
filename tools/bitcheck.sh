#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — cross-toolchain bit-agreement check (SB-C2).
# Copyright (C) 2026 James H. All rights reserved.
#
# Builds the conformance runner with every available compiler at several
# optimization levels and diffs the --bits transcript against the committed
# golden transcript (conformance/expected_bits.txt). Cross-implementation
# bit-agreement is the product; this script is the evidence.
#
#   Usage:  tools/bitcheck.sh            # check against the golden file
#           tools/bitcheck.sh --pin      # (re)generate the golden file
set -u
cd "$(dirname "$0")/.."

GOLDEN=conformance/expected_bits.txt
FLAGS_COMMON="-std=c++17 -Wall -Wextra -Wpedantic -ffp-contract=off -Icore/include"
OPTS=("-O0" "-O1" "-O2" "-O3" "-O3 -march=native")
COMPILERS=()
for cxx in g++ clang++ g++-12 g++-13 g++-14 clang++-17 clang++-18; do
    command -v "$cxx" >/dev/null 2>&1 && COMPILERS+=("$cxx")
done
if [ "${#COMPILERS[@]}" -eq 0 ]; then
    echo "bitcheck: no C++ compiler found" >&2
    exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ "${1:-}" = "--pin" ]; then
    "${COMPILERS[0]}" $FLAGS_COMMON -O2 conformance/runner.cpp -o "$TMP/pin" \
        || exit 2
    "$TMP/pin" --bits > "$GOLDEN"
    echo "bitcheck: pinned golden transcript from ${COMPILERS[0]} -O2:"
    sed 's/^/    /' "$GOLDEN"
    exit 0
fi

if [ ! -f "$GOLDEN" ]; then
    echo "bitcheck: missing $GOLDEN (run tools/bitcheck.sh --pin first)" >&2
    exit 2
fi

fail=0
run=0
for cxx in "${COMPILERS[@]}"; do
    for opt in "${OPTS[@]}"; do
        bin="$TMP/runner_$(echo "$cxx$opt" | tr -c 'a-zA-Z0-9' _)"
        if ! $cxx $FLAGS_COMMON $opt conformance/runner.cpp -o "$bin" \
             2> "$TMP/build.log"; then
            echo "SKIP  $cxx $opt  (does not build here)"
            continue
        fi
        "$bin" --bits > "$TMP/bits.txt"
        run=$((run + 1))
        if diff -q "$GOLDEN" "$TMP/bits.txt" > /dev/null; then
            echo "OK    $cxx $opt"
        else
            echo "FAIL  $cxx $opt  — transcript diverges:"
            diff "$GOLDEN" "$TMP/bits.txt" | sed 's/^/      /'
            fail=1
        fi
    done
done

if [ "$run" -eq 0 ]; then
    echo "bitcheck: nothing ran" >&2
    exit 2
fi
if [ "$fail" -eq 0 ]; then
    echo "bitcheck: ALL $run BUILDS BIT-IDENTICAL (SB-C2 evidence)"
fi
exit "$fail"
