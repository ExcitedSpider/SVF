# GEP operation-state regression

`getGepObjAddrs` must use the state at the operation being evaluated, not the
base pointer's definition node. A null constant has no instruction definition
with a state; using that node aborts with `No preAbsTrace for this node`.
The explicit operation node also keeps dense pointer reads and generated
object entries in the current state. Semi-sparse scalar reads continue to
resolve their own definition sites through `getAbsValue`.

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
