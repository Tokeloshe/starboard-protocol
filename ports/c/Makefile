# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — C99 flight library build.
# Copyright (C) 2026 James H. All rights reserved.
#
#   make              # libstarboard.a + sbctl + transcripts + tests
#   make check        # v1 transcript (normative), v2 transcript, all tests
#   make CC=clang check
#
# Cross-architecture (the same check, run under an emulator):
#   make clean && make CC=aarch64-linux-gnu-gcc \
#        RUN="qemu-aarch64 -L /usr/aarch64-linux-gnu" check
#
# -ffp-contract=off is REQUIRED for GCC (which ignores the in-source
# #pragma STDC FP_CONTRACT); clang honors the pragma, the flag is harmless.

CC      ?= cc
OPT     ?= -O2
CFLAGS  ?= -std=c99 $(OPT) -ffp-contract=off -Wall -Wextra -Wpedantic
LDLIBS  := -lm
RUN     ?=
AR      ?= ar

LIB_SRC := canon.c geometry.c propagator.c covariance.c screening.c rules.c \
           maneuver.c multibody.c broadphase.c sha512.c registry.c \
           ed25519.c cdm.c fsw.c ccsds.c flock.c softdouble.c
LIB_OBJ := $(LIB_SRC:.c=.o)
HDRS    := starboard.h starboard_cdm.h starboard_fsw.h starboard_flock.h \
           starboard_registry.h
GOLDEN     := ../../conformance/expected_bits.txt
GOLDEN_EXT := ../../conformance/expected_bits_ext.txt
EXAMPLES   := ../../examples/cdm

PROGS := sb_bits sb_ext sbctl murmuration test_fsw test_fsw_fi test_flock \
         test_softdouble

all: libstarboard.a $(PROGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

libstarboard.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

sb_bits: main.c libstarboard.a
	$(CC) $(CFLAGS) main.c libstarboard.a $(LDLIBS) -o $@

sb_ext: ext_bits.c ext_scenarios.h ../../conformance/ext_cdm_gen.h libstarboard.a
	$(CC) $(CFLAGS) ext_bits.c libstarboard.a $(LDLIBS) -o $@

sbctl: sbctl.c libstarboard.a
	$(CC) $(CFLAGS) sbctl.c libstarboard.a $(LDLIBS) -o $@

murmuration: ../../examples/murmuration.c libstarboard.a
	$(CC) $(CFLAGS) -I. ../../examples/murmuration.c libstarboard.a $(LDLIBS) -o $@

test_fsw: tests/test_fsw.c libstarboard.a
	$(CC) $(CFLAGS) tests/test_fsw.c libstarboard.a $(LDLIBS) -o $@

# The fault-injection build recompiles the library with the SEU hook.
test_fsw_fi: tests/test_fsw.c $(LIB_SRC) $(HDRS)
	$(CC) $(CFLAGS) -DSB_FSW_FAULT_INJECTION tests/test_fsw.c $(LIB_SRC) \
	    $(LDLIBS) -o $@

test_softdouble: tests/test_softdouble.c softdouble.c
	$(CC) $(CFLAGS) tests/test_softdouble.c softdouble.c $(LDLIBS) -o $@

test_flock: tests/test_flock.c libstarboard.a
	$(CC) $(CFLAGS) tests/test_flock.c libstarboard.a $(LDLIBS) -o $@

# Transcripts land beside the build (not a fixed /tmp path): parallel checks
# from two checkouts must not race on a shared world-writable file.
.PHONY: check check-bits check-ext check-tests clean
check: check-bits check-ext check-tests

check-bits: sb_bits
	$(RUN) ./sb_bits > sb_c_bits.txt
	@if diff -q $(GOLDEN) sb_c_bits.txt >/dev/null; then \
	  echo "SB-C2 v1: C99 port == golden transcript (byte-identical)"; \
	else echo "FAIL: v1 transcript diverges:"; diff $(GOLDEN) sb_c_bits.txt; exit 1; fi

check-ext: sb_ext
	$(RUN) ./sb_ext > sb_c_ext.txt
	@if diff -q $(GOLDEN_EXT) sb_c_ext.txt >/dev/null; then \
	  echo "SB-C2 v2: C99 port == extended golden (byte-identical)"; \
	else echo "FAIL: v2 transcript diverges:"; diff $(GOLDEN_EXT) sb_c_ext.txt; exit 1; fi

check-tests: sbctl test_fsw test_fsw_fi test_flock
	$(RUN) ./sbctl selftest
	$(RUN) ./test_fsw $(EXAMPLES)
	$(RUN) ./test_fsw_fi $(EXAMPLES)
	$(RUN) ./test_flock

clean:
	rm -f $(LIB_OBJ) libstarboard.a $(PROGS) sb_c_bits.txt sb_c_ext.txt
