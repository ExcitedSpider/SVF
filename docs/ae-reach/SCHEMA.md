# Reachability report, version 1

The opt-in invocation is `ae -ae-reach=<report.json> -ae-sparsity=<mode> input.ll`. It accepts one LLVM input module. Set `AE_REACH_TOOL_COMMIT` and `AE_REACH_SVF_COMMIT` to populate the tool revision and the pinned upstream revision. Direct invocations without these environment variables report `unknown` for the revisions.

| Field | Type and meaning |
|---|---|
| `schema_version` | Integer `1` |
| `tool`, `tool_commit`, `svf_commit` | Tool name `ae-reach`, prototype revision, and upstream baseline revision |
| `mode` | `dense`, `semi-sparse`, or `sparse` |
| `command` | Nonempty array of the original command arguments |
| `status` | `ok`, `crash`, `timeout`, or `error` |
| `seconds`, `peak_kib` | Elapsed analysis seconds and optional Linux peak resident size in KiB |
| `sites` | Array of site objects. A failed run uses an empty array |
| `verdict` | `safe` when the run has at least one discovered site and all are safe, `unsafe` when any site has a confirmed violation, `unknown` otherwise. Failed runs use `error` |
| `task`, `suite`, `stock_status` | Evaluation identifiers and matching stock run status, added by the driver |
| `diagnostic` | Optional failure detail supplied by the driver |
| `replay_audit` | Optional counts of candidate concrete executions and confirmed counterexamples to candidate safety proofs, added by the driver |

| Site field | Type and meaning |
|---|---|
| `location` | Object with integer `line` and `column` and string `file` and `function`. The conformance macro line takes precedence over the debug line |
| `kind` | `assert`, `eq`, `unreachable`, or `reach_error` |
| `verdict`, `reason` | Site verdict and justification rule |
| `coverage` | Array of strings describing the coverage certificate or its gaps |
| `ae_state_present` | Boolean indicating whether AE retained a state at the call |
| `abstract_values` | AE interval strings or null when no integer interval is available. Includes `condition` for assert and equality and `left` and `right` for equality. The equality condition follows C's comparison conversions |
| `depends_on` | Source lines of potential earlier unproven checks in the same function. This conservative list can include checks whose refinement had no effect on the verdict |
| `witness`, `replay` | For `unsafe`, a decimal integer array and the concrete replay object's `exit_code` and `line`. Line `-1` identifies a SV-COMP violation at the task level |

`run.py` checks the driver-finalized reports and regenerates both JSON Lines files from them. The public conformance manifest supplies the expected inventory of lines, including lines in failed runs. [Example report](example.json)
