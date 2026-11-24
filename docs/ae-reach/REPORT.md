# Native SVF-AE reachability prototype

## 1. Conformance

The public scorer found zero FALSE_SAFE and zero BOGUS_UNSAFE across 81 site-mode verdicts.

| Case and line | Truth | Dense | Semi-sparse | Sparse |
|---|---|---|---|---|
| c01:4 | safe | safe | safe | safe |
| c02:4 | unsafe | unsafe | unsafe | unsafe |
| c03:4 | unsafe | unsafe | unsafe | unsafe |
| c04:5 | safe | safe | safe | safe |
| c05:6 | safe | safe | safe | safe |
| c06:4 | unsafe | unsafe | unsafe | unsafe |
| c06:5 | safe | safe | safe | safe |
| c07:5 | safe | safe | safe | safe |
| c07:6 | unsafe | unsafe | unsafe | unsafe |
| c08:5 | safe | safe | safe | safe |
| c09:6 | safe | safe | safe | safe |
| c10:8 | unsafe | unsafe | unsafe | unsafe |
| c11:7 | safe | unknown | unknown | unknown |
| c12:4 | safe | unknown | unknown | unknown |
| c12:7 | unsafe | unsafe | unsafe | unsafe |
| c13:4 | not-safe | unknown | unknown | unknown |
| c14:4 | unsafe | unsafe | unsafe | unsafe |
| c15:10 | safe | unknown | unknown | unknown |
| c16:5 | unsafe | unsafe | unsafe | unsafe |
| c16:6 | safe | safe | safe | safe |
| c17:7 | safe | safe | safe | safe |
| c18:5 | safe | unknown | unknown | unknown |
| c18:7 | safe | unknown | unknown | unknown |
| c19:6 | safe | unknown | unknown | unknown |
| c19:7 | unsafe | unsafe | unsafe | unsafe |
| c20:3 | safe | safe | safe | safe |
| c20:7 | safe | unknown | unknown | unknown |

## 2. SV-COMP

Stock AE and the native prototype completed all 681 invocations, with zero crashes, timeouts or compile errors.

| Mode | Safe | Unsafe | Unknown | Crash | Timeout | Median s | Max s |
|---|---:|---:|---:|---:|---:|---:|---:|
| Dense | 2 | 40 | 185 | 0 | 0 | 0.020 | 4.715 |
| Semi-sparse | 2 | 40 | 185 | 0 | 0 | 0.010 | 1.005 |
| Sparse | 2 | 40 | 185 | 0 | 0 | 0.009 | 0.735 |

## 3. Safety rules

All three rules require a closed direct-call CFG traversal from `main`.

| Rule | Soundness argument | Site-mode uses |
|---|---|---:|
| `closed-cfg-unreachable` | Every feasible successor and direct callee remains in the traversal after pruning with LLVM-proven predicates and required termination. | 15 |
| `literal-nonzero-condition` | Folding the integer condition yields an exact, nonzero value. | 6 |
| `dominating-predicate` | LLVM proves the check predicate holds at the call under the passing edges of earlier checks and assumptions. | 15 |

## 4. Soundness spot-check

The driver attempted 2,772 boundary and random executions across the 12 sources supporting 36 safe site-mode verdicts and found zero counterexamples. The two SV-COMP proofs were reviewed at source level. In `nested_1`, the loop exits at exactly six. In `heavy-2`, the checked scalar stays one and the concrete audit uses a short cutoff for the three large nested loops. The driver verified all 147 unsafe site-mode records with the kit replay script, sharing identical witnesses across modes.

## 5. Limits

Indirect calls and unmodelled externals leave coverage open, including the array case's `llvm.memcpy`. Dense AE reported `[1, 1]` at that case's second check, which replay disproved with input 3. LLVM's local predicate facts give limited precision for memory, recursion, relations between variables and interprocedural returns. Site dependencies are conservative within a function. The concrete search uses bounded input sequences and requires a unique SV-COMP site for line `-1` to identify a call.

## 6. Architecture

`ae -ae-reach=<report.json>` inventories calls before building SVF's ICFG. It keeps each check at its original call and inserts an explicit passing branch or termination immediately after it. AE uses its existing branch transfer in each sparsity mode. After AE finishes, the reporter reads the call states and checks any safety claim against the LLVM CFG and predicate facts. This avoids attributing a safety result to a missing trace entry or treating a widened interval as a proof of coverage. The driver handles failed runs, confirms witnesses with the kit's replay script, validates the reports and regenerates both scoring files.

```sh
bash docs/ae-reach/reproduce.sh ../ae-reach-eval ../ae-reach-results/native
```

## 7. Structured output

`SVFBugReport::dumpToJsonFile` writes recorded defect locations and events. Reachability needs a per-run envelope and verdicts for every site, plus interval, coverage, dependency and replay evidence. Both use the existing cJSON library. A separate envelope preserves the existing defect schema. [Schema](https://github.com/ExcitedSpider/SVF/blob/poc/ae-reach/docs/ae-reach/SCHEMA.md) · [Example report](https://github.com/ExcitedSpider/SVF/blob/poc/ae-reach/docs/ae-reach/example.json)

## 8. SVF change

The SVF change adds 498 lines in two new C++ files and changes two registration files by +7/−3 lines. The driver and documentation add 596 lines across seven new files.

The final build passed all 1,297 AE CTest cases. The default-mode comparison covered 2,859 pairs across 865 bitcode inputs using all applicable AE flags and all three modes. Of those, 2,829 matched byte for byte and 30 differed only in the absolute source directory printed in assertion diagnostics. Exit codes, stdout and stderr matched after normalizing that build path. An explicit `svf_assert(0)` probe also produced SIGABRT in both builds in every mode.

An out-of-tree client could own the property vocabulary, CFG coverage audit, JSON, replay and conversion. An AE API would expose call-site enumeration and source/operand mapping, candidate callees with a resolution status, a call transfer hook returning the passing state or an empty continuation, and a read-only query for the final state at the call.
