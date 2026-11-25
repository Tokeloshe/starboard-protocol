#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds site/starboard.wasm: the Starboard flight library (dist/starboard.c)
# plus the browser shim, for the zero-install page site/try.html.
# Needs clang, wasm-ld and wasi-libc (Ubuntu: apt install clang lld wasi-libc).
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/amalgamate.py >/dev/null
SYSROOT=${WASI_SYSROOT:-/usr}
clang --target=wasm32-wasi --sysroot="$SYSROOT" -std=c99 -O2 -ffp-contract=off \
  -Idist -nostartfiles -Wl,--no-entry -Wl,--strip-all \
  -Wl,--export=sbw_input -Wl,--export=sbw_me_buf -Wl,--export=sbw_output \
  -Wl,--export=sbw_input_cap -Wl,--export=sbw_selftest -Wl,--export=sbw_decide \
  dist/starboard.c ports/wasm/starboard_wasm.c -lm -o site/starboard.wasm
mkdir -p site/examples && cp examples/cdm/*.cdm site/examples/
echo "wrote site/starboard.wasm ($(wc -c < site/starboard.wasm) bytes)"
