# case_028 Fix Report

## Problem summary
- `make run_c910_test` produced a coremark failure:
  - Log: `build/logs/c910/c910_coremark_20260207_013632.log`
  - Signature: "There is no instructions retired in the last 50000 cycles!"
- User requested focus on `f_spsram_large`.

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_028`
- DUT top: `f_spsram_large`
- RTL sources: `f_spsram_large.v`, `ram.v`
- Coverage:
  - Ref: 100.00% (139/139)
  - Wolf: 99.17% (119/120)

## Root cause analysis
- No functional mismatch reproduced between ref and wolf in the focused `f_spsram_large` testbench.
- Wolf emits warnings about initial-block writes without edge-sensitive timing control in `f_spsram_large`, but the observed behavior in this TB matches the reference.
- The original coremark stall remains unresolved; likely due to a different module or integration interaction not covered by this minimal case.

## Fix details
- No code changes to `wolf-sv-parser` were made; no reproducible issue found in this reduced case.

## Validation results
- `make -C tests/data/openc910/bug_cases/case_028 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_028 run_c910_bug_case`

## Open questions / follow-ups
- Provide a failing wolf log that highlights a specific module or a diff between ref/wolf traces.
- Confirm whether the stall only happens at the full-system level and whether it correlates with memory initialization, reset sequencing, or other subsystems beyond `f_spsram_large`.
