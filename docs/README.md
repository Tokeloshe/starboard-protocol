# Starboard documentation

Reading order depends on who you are:

| You are… | Start with |
|---|---|
| **Curious** what this is | the [root README](../README.md), then the demo (`examples/`) |
| **An engineer** evaluating the design | [`DESIGN.md`](DESIGN.md) — the normative specification: the rules, the four theorems, every pinned constant. Written to be verified by inspection. |
| **Implementing** Starboard | [`DESIGN.md`](DESIGN.md) §3–§5 (canon, screening, rules), then [`CONFORMANCE.md`](CONFORMANCE.md) — your implementation is correct when it reproduces the vectors and the bit transcript, and *not before* |
| **Integrating comms** | [`PROTOCOL.md`](PROTOCOL.md) — the optional Layer-2 handshake wire format (comms confirm; they never decide) |
| **Thinking about trust/security** | [`REGISTRY.md`](REGISTRY.md) — signed capability assertions, the Merkle log, revocation, and the threat table |
| **Deciding whether to depend on this** | [`OPEN-PROBLEMS.md`](OPEN-PROBLEMS.md) — the honest ledger of what SB/1 does *not* yet settle |
| **Wondering about the business model** | [`STRATEGY.md`](STRATEGY.md) — why the spec is free, why forking loses, how the project sustains itself |

Two conventions used throughout:

- **Normative vs informative.** `DESIGN.md`, the conformance vectors, and the
  pinned bit transcript are normative — they *define* the protocol. Sections
  marked *(informative)* explain but do not bind.
- **The bits are the spec.** Where prose and the pinned transcript could ever
  disagree, the discipline is: fix the prose, version the ruleset — never
  quietly reinterpret the bits (`DESIGN.md` §11).
