# SPDX-License-Identifier: AGPL-3.0-or-later
# Starboard Protocol — CBMC proofs (bit-precise, all inputs).
#   make -C verification/cbmc            # every proof
#   make -C verification/cbmc theorem1   # one proof
C := ../../ports/c
SAFETY := --bounds-check --pointer-check --signed-overflow-check \
          --undefined-shift-check --div-by-zero-check
CBMC := cbmc -I$(C) --function harness

.PHONY: all theorem1 cid evaluate notice cdm
# `evaluate` (real predicates over fully symbolic doubles) is a heavy
# optional target: it did not finish in 15 min on a 2026 laptop-class VM.
# `cdm` (the whole CDM parser, including its floating-point frame
# conversions) is likewise optional: it did not finish in 25 min even at a
# 12-byte input bound. The parser is covered by fuzzing instead.
all: theorem1 cid notice

# Theorem 1, decision layer: every tier/trust/capability, every id pair,
# every epoch, EVERY possible predicate function.
theorem1:
	$(CBMC) theorem1_logic.c $(C)/rules.c -DSB_PROOF_ABSTRACT_PREDICATES $(SAFETY)

# Theorem 1, conjunction id: the hashed message is order-independent.
cid:
	$(CBMC) lemma_cid.c $(C)/rules.c $(SAFETY)

# The real predicates: well-formed verdict and no UB for every double.
evaluate:
	$(CBMC) evaluate_safety.c $(C)/rules.c $(C)/geometry.c $(C)/canon.c $(SAFETY)

# Inter-satellite input: any 204-byte frame into the flock ledger.
notice:
	$(CBMC) notice_safety.c $(C)/flock.c $(C)/ccsds.c $(C)/rules.c \
	    $(C)/geometry.c $(C)/canon.c $(C)/propagator.c $(C)/maneuver.c \
	    $(C)/covariance.c $(SAFETY) --unwind 210 --unwinding-assertions

# Uplink input: any byte string (bounded length) into the CDM parser.
cdm:
	$(CBMC) cdm_safety.c $(C)/cdm.c $(C)/rules.c $(C)/geometry.c \
	    $(C)/canon.c $(C)/maneuver.c $(C)/covariance.c $(C)/propagator.c \
	    $(C)/screening.c $(SAFETY) --unwind 45
