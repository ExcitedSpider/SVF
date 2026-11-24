# Out-of-tree AE reachability

An external `ae-reach` driver can link against a pinned SVF build and leave the stock `ae` executable intact. The driver owns property selection, verdicts, and JSON output.

## What works today

At `a8a0f51`, an external driver can build the PAG, call `getAEInstance()`, and inspect the ICFG after `runOnModule()` returns. It can also register an `AEDetector` with `addDetector()`. AE invokes `detect()` on visited nodes and `reportBug()` after analysis. A small external probe linked against the pinned libraries read call sites, state presence, and assertion argument intervals in all three modes.

Site discovery, interval verdicts, and JSON can move out of tree today. The PoC uses protected `allAnalyzedNodes` and `isBranchEdgeFeasible()` for coverage and branch checks. An external detector can record visits in `detect()` and implement a limited Boolean edge check. A visit alone gives no information about skipped calling contexts.

## One-time SVF interface

Expose a read-only final-results view with ICFG access, non-mutating mode-aware value lookups, and coverage and edge-feasibility queries that can return `unknown`. Define the lifetime of the view and when a state covers the relevant analysis contexts. The current public `getTrace()` and `getAbsState()` return mutable data, and some value lookups can populate state. The existing registration and completion mechanisms suffice. For process isolation, a trusted driver can serialize the view and run feature code in a separate process.

## Enforcement

- A full commit pin identifies the source. A maintainer-controlled worker fetches a fresh checkout, checks its tracked tree against the pin, and rejects extra files. A clean checkout by itself says little about the binary that runs.
- Read-only source mounts prevent writes through those mounts. An empty out-of-tree build keeps generated objects separate. Use a trusted build recipe with pinned tools and dependencies so overlays, include paths, and link inputs come from controlled locations.
- A verifier outside the agent's write and execution privileges builds the artifacts, attests their digests, and checks the digests and runtime dependencies before execution. Local hooks and agent-supplied logs are evidence only if the trusted verifier independently reproduces them. This scheme establishes source and artifact provenance within the trusted environment. Extension verdicts still need their own validation.

## Behaviour checks

Build stock AE from the pin and compare it with the candidate over the same Test-Suite bitcode, flags, and environment. Check stdout, stderr, exit codes or signals, and generated files. Normalize only known nondeterministic fields. The PoC comparison matched all 1,297 registered AE test invocations with statistics disabled and reachability off. Corpus comparisons can miss untested inputs and flags, internal state differences, and performance regressions.
