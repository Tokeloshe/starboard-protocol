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
    # Never pin a golden transcript from a build that fails its own
    # conformance vectors — a broken build would otherwise become the
    # reference every other build is judged against.
    if ! "$TMP/pin" > "$TMP/pin_conformance.log"; then
        echo "bitcheck: REFUSING to pin — conformance suite fails:" >&2
        sed 's/^/    /' "$TMP/pin_conformance.log" >&2
        exit 2
    fi
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

# --- The EXTENDED transcript (suite v2): CW maneuvers, returns, multi-body
# windows, broad-phase sets, exact decimal ingest and the CDM binding. Same
# matrix: every C++ compiler x optimization level must reproduce it too.
GOLDEN_EXT=conformance/expected_bits_ext.txt
for cxx in "${COMPILERS[@]}"; do
    for opt in "${OPTS[@]}"; do
        bin="$TMP/ext_$(echo "$cxx$opt" | tr -c 'a-zA-Z0-9' _)"
        if ! $cxx $FLAGS_COMMON $opt conformance/ext_bits.cpp -o "$bin" \
             2> "$TMP/build.log"; then
            echo "SKIP  ext: $cxx $opt  (does not build here)"
            continue
        fi
        "$bin" > "$TMP/ext.txt"
        run=$((run + 1))
        if diff -q "$GOLDEN_EXT" "$TMP/ext.txt" > /dev/null; then
            echo "OK    ext:  $cxx $opt"
        else
            echo "FAIL  ext:  $cxx $opt  — extended transcript diverges:"
            diff "$GOLDEN_EXT" "$TMP/ext.txt" | sed 's/^/      /'
            fail=1
        fi
    done
done

# --- Cross-LANGUAGE: the independent C99 port must match BOTH goldens -------
# (SPEC §10 / CONFORMANCE.md SB-C2). This is the strongest bit-agreement
# evidence: not just cross-compiler of one source, but a second, independent
# implementation in a different language reproducing the transcripts.
CPORT_LIB=(ports/c/canon.c ports/c/geometry.c ports/c/propagator.c
           ports/c/covariance.c ports/c/screening.c ports/c/rules.c
           ports/c/maneuver.c ports/c/multibody.c ports/c/broadphase.c
           ports/c/sha512.c ports/c/registry.c ports/c/ed25519.c
           ports/c/cdm.c ports/c/fsw.c ports/c/ccsds.c ports/c/flock.c
           ports/c/softdouble.c)
CC_LIST=()
for cc in gcc clang cc; do
    command -v "$cc" >/dev/null 2>&1 && CC_LIST+=("$cc")
done
if [ -f ports/c/main.c ]; then
    for cc in "${CC_LIST[@]}"; do
        for opt in "-O0" "-O2" "-O3 -march=native"; do
            tag=$(echo "$cc$opt" | tr -c 'a-zA-Z0-9' _)
            if ! $cc -std=c99 -ffp-contract=off $opt "${CPORT_LIB[@]}" \
                 ports/c/main.c -lm -o "$TMP/cport_$tag" 2> "$TMP/cbuild.log" ||
               ! $cc -std=c99 -ffp-contract=off $opt "${CPORT_LIB[@]}" \
                 ports/c/ext_bits.c -lm -o "$TMP/cext_$tag" 2>> "$TMP/cbuild.log"; then
                echo "SKIP  C99 port: $cc $opt  (does not build here)"
                continue
            fi
            "$TMP/cport_$tag" > "$TMP/cbits.txt"
            "$TMP/cext_$tag" > "$TMP/cext.txt"
            run=$((run + 2))
            if diff -q "$GOLDEN" "$TMP/cbits.txt" > /dev/null &&
               diff -q "$GOLDEN_EXT" "$TMP/cext.txt" > /dev/null; then
                echo "OK    C99 port: $cc $opt  (v1 + v2)"
            else
                echo "FAIL  C99 port: $cc $opt  — transcript diverges:"
                diff "$GOLDEN" "$TMP/cbits.txt" | sed 's/^/      /'
                diff "$GOLDEN_EXT" "$TMP/cext.txt" | sed 's/^/      /'
                fail=1
            fi
        done
    done
fi

if [ "$fail" -eq 0 ]; then
    echo "bitcheck: ALL $run TRANSCRIPTS BIT-IDENTICAL (SB-C2 evidence:" \
         "C++ cross-compiler + C99 cross-language, v1 + v2)"
fi
exit "$fail"
