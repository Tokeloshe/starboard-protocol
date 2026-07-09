# Contributing to Starboard

Thanks for your interest — issues, discussion, and review are very welcome.
Code contributions come with one important condition, explained below.

## Bug reports & discussion (always welcome, no paperwork)

- **Found a disagreement?** If two builds or two implementations of the spec
  produce different bits on the verdict path, that is the highest-severity
  bug this project can have. Open an issue with your platform, compiler,
  flags, and the diff of `--bits` transcripts (`tools/bitcheck.sh`).
- **Found a spec ambiguity?** If you implemented from `docs/DESIGN.md` alone
  and had to guess anywhere, that guess is a spec bug — please report it.
- **Conformance vector failures**, math errors, doc mistakes: open an issue
  with a minimal reproduction.

## Code contributions require a CLA

Starboard is dual-licensed (AGPL-3.0 + commercial, see
`COMMERCIAL-LICENSE.md`), which is only possible while the copyright of the
codebase remains with a single holder. Therefore:

> **Pull requests are not merged without a signed Contributor License
> Agreement** assigning or broadly licensing your contribution's rights to
> the project's copyright holder. If you open a PR, you'll be asked to
> confirm this first.

If you'd rather not sign a CLA, that's completely fine — file an issue
describing the fix instead, and it will be implemented independently with
credit to you.

## The bar for changes

- **Nothing normative changes casually.** Constants, algorithms, encodings,
  and operation order on the verdict path are part of the protocol's
  identity: changing any of them changes the bits, which changes the
  `RULESET_ID` (see `docs/DESIGN.md` §11). Normative changes need a spec
  revision, regenerated anchors and transcript, and both implementations
  (C++ and C99) updated in lockstep.
- **Every change must keep the matrix green**: `ctest` (all vectors + the
  cross-language bit-match) and `tools/bitcheck.sh` must pass on gcc and
  clang before and after.
- **Style**: match the surrounding code. Core is allocation-free,
  exception-free, recursion-free; `ports/c/` is MISRA-C99-oriented. No new
  dependencies — dependency-freedom is a feature.

## Quick verification loop

```
cmake -B build && cmake --build build
ctest --test-dir build          # 21 vectors + cross-language bit-match
tools/bitcheck.sh               # full compiler × language matrix
```
