# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — C99 port: crypto + registry tests.
# Copyright (C) 2026 James H. All rights reserved.
#
#   make test        # test_crypto: native, -m32 (if the toolchain supports
#                    #   it; skipped quietly otherwise), ASan+UBSan
#   make diff        # diff_ed25519: C port vs C++ header, >= 20000 cases
#   make wycheproof  # every Wycheproof Ed25519 vector vs C port AND C++
#   make all-tests   # all of the above
#   make CC=clang CXX=clang++ all-tests
#
# Everything builds with -Werror: the port must stay warning-free on gcc
# and clang.

CC       ?= cc
CXX      ?= c++
PYTHON   ?= python3
WARN     := -Wall -Wextra -Wpedantic -Werror
CFLAGS   ?= -std=c99 -O2 -ffp-contract=off $(WARN)
CXXFLAGS ?= -std=c++17 -O2 -ffp-contract=off $(WARN)
SANFLAGS := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
            -fno-sanitize-recover=all
LDLIBS   := -lm

PORT     := ..
CORE_INC := ../../../core/include
FIXT_HPP := ../../../conformance/registry_fixtures.hpp
# Every port TU except the transcript emitter (main.c).
LIB_SRC  := $(PORT)/canon.c $(PORT)/geometry.c $(PORT)/propagator.c \
            $(PORT)/covariance.c $(PORT)/screening.c $(PORT)/rules.c \
            $(PORT)/sha512.c $(PORT)/registry.c $(PORT)/ed25519.c
HDRS     := $(PORT)/starboard.h $(PORT)/starboard_registry.h
OUT      := build

.PHONY: test diff wycheproof all-tests clean

all-tests: test diff wycheproof

# The C fixture header is transcribed from the C++ one (same bytes).
registry_fixtures_c.h: $(FIXT_HPP) gen_c_fixtures.py
	$(PYTHON) gen_c_fixtures.py $(FIXT_HPP) > $@.tmp && mv $@.tmp $@

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_crypto: test_crypto.c registry_fixtures_c.h $(LIB_SRC) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -I$(PORT) -I. test_crypto.c $(LIB_SRC) $(LDLIBS) -o $@

$(OUT)/test_crypto_san: test_crypto.c registry_fixtures_c.h $(LIB_SRC) $(HDRS) | $(OUT)
	$(CC) -std=c99 -ffp-contract=off $(WARN) $(SANFLAGS) -I$(PORT) -I. \
	    test_crypto.c $(LIB_SRC) $(LDLIBS) -o $@

test: $(OUT)/test_crypto $(OUT)/test_crypto_san
	@echo "== test_crypto (native)"
	./$(OUT)/test_crypto
	@echo "== test_crypto (-m32)"
	@if echo 'int main(void){return 0;}' | \
	    $(CC) -m32 -x c - -o $(OUT)/m32probe >/dev/null 2>&1; then \
	  $(CC) -m32 $(CFLAGS) -I$(PORT) -I. test_crypto.c $(LIB_SRC) $(LDLIBS) \
	      -o $(OUT)/test_crypto_m32 && ./$(OUT)/test_crypto_m32; \
	else \
	  echo "   (skipped: $(CC) -m32 unavailable — no multilib)"; \
	fi
	@echo "== test_crypto (ASan+UBSan)"
	./$(OUT)/test_crypto_san

# Differential test: the C objects (extern "C") vs the C++ header.
$(OUT)/diff_ed25519: diff_ed25519.cpp $(LIB_SRC) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -c $(PORT)/sha512.c -o $(OUT)/diff_sha512.o
	$(CC) $(CFLAGS) -c $(PORT)/ed25519.c -o $(OUT)/diff_ed25519_c.o
	$(CXX) $(CXXFLAGS) -I$(PORT) -I$(CORE_INC) diff_ed25519.cpp \
	    $(OUT)/diff_sha512.o $(OUT)/diff_ed25519_c.o -o $@

diff: $(OUT)/diff_ed25519
	./$(OUT)/diff_ed25519

# Wycheproof: python flattens the JSON, a C++ driver runs both verifiers.
$(OUT)/wycheproof_ed25519: wycheproof_ed25519.cpp $(LIB_SRC) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -c $(PORT)/sha512.c -o $(OUT)/wp_sha512.o
	$(CC) $(CFLAGS) -c $(PORT)/ed25519.c -o $(OUT)/wp_ed25519_c.o
	$(CXX) $(CXXFLAGS) -I$(PORT) -I$(CORE_INC) wycheproof_ed25519.cpp \
	    $(OUT)/wp_sha512.o $(OUT)/wp_ed25519_c.o -o $@

wycheproof: $(OUT)/wycheproof_ed25519
	$(PYTHON) -I run_wycheproof.py vectors/ed25519_test.json ./$(OUT)/wycheproof_ed25519

clean:
	rm -rf $(OUT)
