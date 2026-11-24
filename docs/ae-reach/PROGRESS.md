# Progress

- 2026-11-24 22:18 AEDT. Created `poc/ae-reach` and a separate detached baseline at `a8a0f51`. Built stock AE in the LLVM 22 image with assertions enabled. CMake's compile database link required a writable copy of each checkout, an existing target file, and configuration from the source directory.
- 2026-11-24 22:23 AEDT. Built the opt-in report path and ran the initial 60 conformance invocations. Final AE states sometimes carry top on simple Boolean checks and omit 64-bit constants. Used LLVM's integer folding and context predicates as independent certificates and kept AE's intervals visible in the report.
- 2026-11-24 22:26 AEDT. Rebuilt after adding the certificates. The witness search found the loop violation after extracting integer operands beyond the first operand of each instruction. Started `ctest -R ae_` and the all-bitcode default comparison. The configured GitHub token failed `gh auth status`.
