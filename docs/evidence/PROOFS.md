# Evidence — machine-checked proofs (CBMC)

`docs/DESIGN.md` proves Theorems 1–4 by hand; `OPEN-PROBLEMS.md` #8 asks for
machine-checked proofs. This directory (`verification/cbmc/`) provides them
for the C99 flight implementation using **CBMC**, a bounded model checker
with bit-precise IEEE-754 and integer semantics. CBMC does not sample
inputs: it encodes *every* input bit pattern symbolically and either proves a
property for all of them or produces a counterexample.

All proofs also run CBMC's safety checks: array bounds, pointer validity,
signed overflow, undefined shifts, division by zero.

| Proof | Property | Inputs covered | Result |
|---|---|---|---|
| `theorem1` | **Theorem 1, decision layer.** `evaluate(A,B)` and `evaluate(B,A)` name the same give-way craft, by the same rule, with the same advisory flag; the named craft is one of the pair; roles are complementary; a pairwise rule always decides; and the Table 8-1 trust rules hold | every tier, trust level and capability of both craft; every pair of 12-byte ids; every 64-bit epoch; **every possible predicate function** | ✅ 174 properties, ~1 s |
| `cid` | **Theorem 1, conjunction id.** The 36-byte message hashed into the conjunction id is identical from both perspectives, orders the pair canonically, and embeds `RULESET_ID` | every pair of ids (equal or not), every epoch | ✅ 215 properties, < 1 s |
| `notice` | **Inter-satellite input safety.** Any 204-byte frame into the flock ledger: no UB or memory error; every frame accounted for exactly once; a relay is nothing or exactly one notice | all 2¹⁶³² frames | ✅ 1 601 properties, ~5 s |
| `cdm` | **Uplink input safety.** Any byte string up to 40 bytes into the CDM parser: no UB or memory error; a defined status; an error line inside the input | all strings ≤ 40 bytes | ⏳ did not finish in 25 min, even at a 12-byte bound (the parser's floating-point frame conversions are bit-blasted); optional target. The parser is covered by ≈129 M fuzz executions under ASan/UBSan and the C-vs-C++ differential instead |
| `evaluate` | The *real* overtaking/starboard predicates over fully symbolic doubles | every double bit pattern, NaN/∞ included | ⏳ did not finish in 15 min; optional target |

## How the Theorem 1 proof is structured

Theorem 1 says both craft compute the same verdict. It is proved in two
parts that compose:

1. **The decision layer is symmetric for any predicates** (`theorem1`). The
   two geometric predicates are replaced by *arbitrary* functions of their
   ordered argument pair: four free booleans, one per (predicate, order).
   CBMC proves the rule chain, trust projection and canonical pair transform
   agree from both perspectives for all of them. So Theorem 1 holds for any
   deterministic predicates.
2. **The conjunction id is symmetric** (`cid`). Its message is identical
   from both sides, and FNV-1a is a pure function of the message.

The premise that joins them is that the real predicates are pure functions
of their two arguments: they read nothing else (no globals, no state). That
holds by inspection of `rules.c` and the C semantics of `const` value
arguments. Their freedom from undefined behaviour over every double bit
pattern is covered by the `evaluate` target when it completes, and in
practice by ASan/UBSan, the sanitizer CI job, and the `fuzz_fsw` target.

**Why the split.** A single monolithic proof asks the SAT solver to show two
copies of a 36-round 64-bit FNV hash are equivalent through multiplexed
inputs, which is notoriously hard for SAT. It did not finish in 25 minutes.
Separating the message lemma makes each part a ~1 s proof.

The only change to flight code was bit-neutral: `sb_conjunction_id` now
calls `sb_conjunction_id_message` + `sb_fnv1a64` instead of an inline loop,
and `rules.c` honours `SB_PROOF_ABSTRACT_PREDICATES`, which only the proof
harness defines.

## Reproduce

```
apt install cbmc
make -C verification/cbmc          # theorem1 cid notice
make -C verification/cbmc evaluate # optional, heavy
make -C verification/cbmc cdm      # optional, heavy
```
