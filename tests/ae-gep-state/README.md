# GEP missing-definition-node regression

`SVFIRBuilder::addNullPtrNode` explicitly creates the canonical null-pointer
value with a null ICFG node. `ValVar::getICFGNode` returns that stored field.
Passing it to `getAbsState` aborts with `No preAbsTrace for this node`.

`getGepObjAddrs` now checks whether the pointer's definition node is null.
When it exists, the original state lookup and address calculation are
unchanged. Otherwise, the helper uses the global ICFG node, matching
`SemiSparseAbstractInterpretation::getICFGNode`. AE initializes the null
address in that global state. The existing null/black-hole address check
then skips object creation for that address. The helper signature and all
callers remain unchanged; no missing state is fabricated.

Run with an assertions-enabled SVF build and its matching LLVM installation:

```sh
cmake -DAE="$SVF_BUILD/bin/ae" \
  -DLLVM_BIN="$LLVM_BIN" \
  -DEXTAPI="$SVF_BUILD/lib/extapi.bc" \
  -DOUTPUT_DIR="$SVF_BUILD/gep-state-results" \
  -P tests/ae-gep-state/run.cmake
```

The standalone runner needs no external Test-Suite checkout. It checks both
dense and semi-sparse AE, requires successful completion, and verifies one
null-dereference report for `null-base.ll`. The memory control requires six
verified assertions covering local/global GEP store/load, `memset`, `memcpy`,
and `strlen`. It uses normal Clang lowering to LLVM memory intrinsics.
Every invocation has a 30-second timeout, and logs remain in `OUTPUT_DIR`.

Validated with LLVM 21.1.0 and Z3 4.15.4, Release with
`SVF_ENABLE_ASSERTIONS=ON`, against parent `482b6f7`:

| Case (both modes) | Parent | Fix |
| --- | --- | --- |
| Null GEP, null detector | assertion abort (134) | exit 0, one null report |
| Memory control | exit 0, six verified assertions | unchanged |

This is a focused fix for the missing-state failure reported in
[BANDA-Swift #118](https://github.com/ExcitedSpider/BANDA-Swift/issues/118).
It does not establish that the complete BANDA worker succeeds: enabling
`-overflow` on the null fixture exposes a subsequent buffer-detector SIGSEGV
(139), consistent with the separate
[#119](https://github.com/ExcitedSpider/BANDA-Swift/issues/119) investigation.
That detector failure is not suppressed by this patch. Full-sparse AE and the
external SVF Test-Suite are not covered by this standalone regression.
