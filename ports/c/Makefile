# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — C99 port build.
# Copyright (C) 2026 James H. All rights reserved.
#
# The C99 verdict-path port. `make check` reproduces the reference --bits
# transcript byte-for-byte, the definitive cross-LANGUAGE bit-agreement proof.
#
#   make            # build sb_bits
#   make check      # build + diff --bits against ../../conformance/expected_bits.txt
#   make CC=clang   # (clang honors the in-source FP_CONTRACT pragma too)

CC      ?= cc
# -ffp-contract=off is REQUIRED for GCC (which ignores the in-source
# #pragma STDC FP_CONTRACT); clang honors the pragma but the flag is harmless.
CFLAGS  ?= -std=c99 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic
LDLIBS  := -lm
SRC     := canon.c geometry.c propagator.c covariance.c screening.c \
           rules.c sha512.c registry.c main.c
GOLDEN  := ../../conformance/expected_bits.txt

sb_bits: $(SRC) starboard.h
	$(CC) $(CFLAGS) $(SRC) $(LDLIBS) -o $@

.PHONY: check clean
check: sb_bits
	./sb_bits > /tmp/sb_c_bits.txt
	@if diff -q $(GOLDEN) /tmp/sb_c_bits.txt >/dev/null; then \
	  echo "SB-C2 cross-language: C99 port == golden transcript (byte-identical)"; \
	else \
	  echo "FAIL: C99 port diverges from golden:"; \
	  diff $(GOLDEN) /tmp/sb_c_bits.txt; exit 1; \
	fi

clean:
	rm -f sb_bits
