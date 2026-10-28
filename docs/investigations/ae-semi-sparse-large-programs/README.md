# SVF-AE semi-sparse on large fuzz-target programs

This directory records an investigation of `ae -ae-sparsity=semi-sparse` on
seven large LLVM bitcode programs: two controlled driver programs and five
complete OSS-Fuzz targets from ARVO. It contains everything needed to rerun
the investigation except the bitcode itself.

| File | Purpose |
| --- | --- |
| `cases.csv` | The seven cases: input file name, SHA-256, size, AE entry mode, known bad lines |
| `run_ae.py` | Verifies each input, runs `ae`, and summarises alarms, outcome, time and peak RSS |
| `driver.c` | Source of the planted-bug driver used by the two controlled cases |

## Question

For each program:

1. How many alarms does SVF-AE report?
2. Do the alarms include the known bad line?

## Setup

- SVF `071eb025` (upstream `master`, 2026-10-28), LLVM/Clang 21.1.0, Release build.
- Command, per case:

  ```text
  ae -ae-sparsity=semi-sparse -ae-fun-entry=<entry> -overflow -null-deref <bitcode>
  ```

- `<entry>` is `main` for the two driver cases. The ARVO programs have no
  `main` (their entry is `LLVMFuzzerTestOneInput`), so they use `no-main`,
  which starts from every function without an external caller.
- One case at a time, 24 GiB memory cap, 30-minute timeout per case.
- An alarm site is the `file:line` of an overflow or null-dereference event.
  Sites are collected from the inline events `ae` prints while it runs and
  from its final report, so crashed runs still count the alarms emitted
  before the crash.

## Inputs

The bitcode is not committed. `cases.csv` pins each file by SHA-256, and
`run_ae.py` refuses an input whose hash differs.

**Controlled driver cases.** `driver.c` writes one byte past a one-byte stack
array at `driver.c:8`; `main` calls it once. The driver bitcode is linked with
real modules that are never called from `main`, used only to make the module
large:

| Case | Padding | Defined functions |
| --- | --- | ---: |
| `opensc-driver-10x` | OpenSC `pkcs15`, `card`/`apdu` and `coolkey` modules from ARVO 42479504 | ~1,000 |
| `harfbuzz-opensc-driver-10k` | complete HarfBuzz `hb-subset-fuzzer` bitcode (ARVO 42481119) plus the OpenSC `pkcs15` module | ~12,350 |

**ARVO cases.** Whole-program bitcode of the fuzz target, extracted from the
vulnerable ARVO build (`n132/arvo:<id>-vul`). The known bad lines are the
ARVO crash site plus the ground-truth sites on the crashing path. For OpenSC,
the ARVO crash site is inside the ASan `memcpy` interceptor, so only the
program-side sites are listed.

| ARVO ID | Project | Fuzz target | Project commit | Clang | Defined functions |
| ---: | --- | --- | --- | ---: | ---: |
| 42479504 | OpenSC | `fuzz_pkcs15_reader` | `7a1e42e1352201127d1a97b6cb5f41e48cace0f5` | 10 | 456 |
| 42539004 | ImageMagick (libde265) | `encoder_heic_fuzzer` | `23144676a129a92a84f1ee9d0e648a1c311a1d26` | 7 | 945 |
| 42498356 | c-blosc2 | `decompress_frame_fuzzer` | `7f4574f51ad7db56fc13de89e13e7893b1a82189` | 12 | 921 |
| 42482045 | nDPI | `fuzz_process_packet` | `c6acf97bfbe5ad26db3c2f5dd4d379ac674d6fb3` | 10 | 1,829 |
| 42481119 | HarfBuzz | `hb-subset-fuzzer` | `a32ecc15aec6518f5a126fb8f3643e563327f74d` | 10 | 11,565 |

The ARVO bitcode includes the libFuzzer runtime, so `no-main` also analyses
libFuzzer's own entry functions.

## Reproduce

With an SVF build's `ae` on `PATH` and the seven files in one directory:

```bash
python3 docs/investigations/ae-semi-sparse-large-programs/run_ae.py <bitcode-dir> <out-dir>
```

The script prints a Markdown table like the one below, writes each full `ae`
log to `<out-dir>/<case>.log`, and writes `<out-dir>/results.json` with every
alarm site. Use `--only <case> ...` to run a subset, `--ae <path>` for a
specific binary, and `--timeout <sec>` to change the per-case limit.

## Results

| Case | Entry | Outcome | Wall (s) | Peak RSS (MiB) | Overflow lines | Null-deref lines | Known bad line in alarms? |
|---|---|---|---:|---:|---:|---:|---|
| opensc-driver-10x | main | finished | 20.4 | 2,025 | 1 | 0 | ✅ `driver.c:8` |
| harfbuzz-opensc-driver-10k | main | finished | 34.0 | 4,156 | 1 | 0 | ✅ `driver.c:8` |
| ARVO 42479504 OpenSC | no-main | segfault | 77.7 | 4,149 | 6 | 32 | ❌ none of 7 |
| ARVO 42539004 ImageMagick | no-main | assertion failure | 1.0 | 213 | 3 | 15 | ❌ none of 4 |
| ARVO 42498356 c-blosc2 | no-main | segfault | 16.0 | 2,047 | 0 | 3 | ❌ none of 6 |
| ARVO 42482045 nDPI | no-main | segfault | 30.5 | 5,778 | 5 | 2 | ❌ none of 4 |
| ARVO 42481119 HarfBuzz | no-main | segfault | 27.3 | 7,387 | 0 | 1 | ❌ none of 4 |

Two independent runs gave the same outcomes and alarm counts.

### Driver cases

Both finish and report exactly one alarm, the planted `driver.c:8` overflow.
`ae` analyses only 2 functions (`main` and the bug function): 0.19% and 0.01%
of the module. The padding is unreachable from `main`, so these cases measure
module loading and graph construction, not analysis of a large program.

### ARVO cases

None finishes, and none reaches a known bad line. Every failure happens early,
mostly in harness, logging or libFuzzer code:

| ARVO ID | Alarms emitted before the failure |
| ---: | --- |
| 42479504 | overflow in `log.c` (329–337); null-deref in `fuzz_pkcs15_reader.c` and `log.c` |
| 42539004 | overflow in `FuzzerTracePC.h`, `FuzzerValueBitMap.h`; null-deref in libc++ `sstream`/`string`, `SecurityPolicy.cpp`, `utils.cc` |
| 42498356 | null-deref in `blosc2.c` (178, 179, 3307) |
| 42482045 | overflow in `FuzzerTracePC.cpp`/`.h` (e.g. allocation `[512, 512]`, access `[71575, 71575]`) |
| 42481119 | null-deref in `hb-sanitize.hh:184` |

Memory is not the limit: the largest peak is 7.4 GiB, well under the 24 GiB cap.

### Failure sites

All four segfaults are in the buffer-overflow detector, called while
transferring a node reached through a call:

```text
#0  SVF::BufOverflowDetector::detect(SVF::ICFGNode const*)
#1  SVF::AbstractInterpretation::handleICFGNode(SVF::ICFGNode const*)
#2  SVF::AbstractInterpretation::handleFunction(SVF::ICFGNode const*, SVF::CallICFGNode const*)
#3  SVF::AbstractInterpretation::handleFunCall(SVF::CallICFGNode const*)   (or analyzeFromAllProgEntries)
...
```

ImageMagick (42539004) instead fails an interval invariant after 1 s:

```text
svf/include/AE/Core/IntervalValue.h:122: SVF::IntervalValue::IntervalValue(SVF::BoundedInt, SVF::BoundedInt):
Assertion `(isBottom() || _lb.leq(_ub)) && "lower bound should be less than or equal to upper bound"' failed.
```

The logs also show `Warning: llvm.memset.p0.i64 is not in the rules` before
some crashes; whether it is related has not been checked.

## Conclusions

- On these inputs, SVF-AE semi-sparse can be evaluated only on the driver
  cases, and those exercise two functions.
- On all five complete ARVO programs, `ae` fails before reaching the real bug,
  in four cases inside `BufOverflowDetector::detect`.

## Open follow-ups

- Reduce one segfault to a small C reproducer, starting from the smallest
  input (OpenSC 42479504) with a debug build to locate the faulting access in
  `BufOverflowDetector::detect`.
- Reduce the ImageMagick interval assertion.
- Rerun with `-null-deref` only, to separate the overflow-detector crash from
  the analysis's ability to traverse these programs.
- Compare `-ae-sparsity=dense` and `sparse` on the same inputs.
