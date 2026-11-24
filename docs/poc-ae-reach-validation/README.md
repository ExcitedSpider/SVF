# AE reachability validation

The eight shared cases retain their original expected verdicts. They and the added pruned-branch probe were run in dense, semi-sparse, and full-sparse mode. The [draft PR](https://github.com/ExcitedSpider/SVF/pull/28) has the shared-case results.

## SV-COMP slice

The source was SAAV-bench’s cached extraction of `svcomp25` at the pinned commit `1e5856db49f3a4766f416cc60382aa92012b2939`. The slice contains 27 ControlFlow and 73 Loops tasks, with 50 of each official `unreach-call` label. Selection used the pinned `ControlFlow.set` and `Loops.set` manifests. ControlFlow was restricted to the signed and unsigned integer regression directories and `longjmp`. The selector used single C or preprocessed C inputs up to 8 KiB and excluded ILP32 sources with include directives. For each label, it sorted task IDs by SHA-256, took up to 25 ControlFlow tasks, and filled to 50 from Loops. [Task list](tasks.txt)

Each task ran once in dense mode against PoC commit `0948b17318e451abe4122e6442afe8b2f024e33a`. LLVM 22 compiled the original sources with the task’s ILP32 or LP64 model and ran mem2reg. Each of Clang, opt, and AE had a 30-second timeout. `time_seconds` measures elapsed wall time for all three stages with up to eight tasks in flight. `failure` is null on success and otherwise records the type of failure.

A task was marked `reachable` if any reported site was reachable. It was marked `unreachable` if at least one site was reported and every reported site was unreachable. Mixed unknown results, empty reports, and failures yield `unknown`. This aggregation only covers the PoC’s direct-call inventory. The raw output contains 12 `unreachable` and 88 `unknown` verdicts, with no failures or timeouts. [Raw per-task results](results.json)

## Every task reported as unreachable

Official labels are shown only for these tasks to identify false safes. The raw results are available for independent scoring.

| Task | Official `expected_verdict` | Finding |
|---|---|---|
| `longjmp/68-longjmp_04-counting-local_unknown_1_neg.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_04-counting-local_unknown_1_pos.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_07-returns_true.yml` | `true` | Safe |
| `longjmp/68-longjmp_12-counting-global_unknown_1_neg.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_12-counting-global_unknown_1_pos.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_13-counting-local_unknown_1_neg.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_13-counting-local_unknown_1_pos.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_15-counting-global-one-method_unknown_1_neg.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_15-counting-global-one-method_unknown_1_pos.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_16-counting-local-one-method_unknown_1_neg.yml` | `false` | **False safe** |
| `longjmp/68-longjmp_16-counting-local-one-method_unknown_1_pos.yml` | `false` | **False safe** |
| `loops-crafted-1/Mono5_1.yml` | `false` | **False safe** |
