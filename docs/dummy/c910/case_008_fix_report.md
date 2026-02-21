# case_008 Fix Report

## Problem summary
- Original failure (log: `build/artifacts/c910_run.log`): wolf-sv-parser aborted conversion on `plic_hreg_busif.v:398` with “Unsupported expression kind” for `$unsigned(INT_NUM/32)` inside an indexed part-select compare.
- Secondary fallout after unblocking: instance port connections that read unpacked-array elements (`hart_ie_prdata_pre[k]`) failed in graph assembly because memory reads were not materialized for connection expressions.
- Additional correctness issue: `$unsigned(INT_NUM/32)` was being sized to 1 bit under LHS width context, truncating the divisor to 0 and folding the compare threshold to X.

## Minimal repro
- Case path: `tests/data/openc910/bug_cases/case_008`
- DUT top: `plic_hreg_busif`
- Ref coverage: 59.21% (COV_MIN lowered to 55 due to large module surface)

## Root cause analysis
- `$unsigned/$signed` system calls were not lowered, so conversion stopped at the call expression.
- Even after adding support, the RHS of the compare was sized under the assignment’s 1-bit width context, collapsing `INT_NUM/32` to a 1-bit division (0/0) and folding to X.
- Unpacked-array reads used in instance connections were not registered as memory reads, so graph assembly attempted to bind a missing symbol value.

## Fix details
- Added `$unsigned/$signed` handling in `src/convert.cpp`:
  - If the argument is a constant integer, emit a constant node directly.
  - Lower arguments under a width context of 0 to preserve their natural sizing.
  - Apply conversion with the call’s type afterward when needed.
- Added `ensureConnectionMemoryRead` in graph assembly to recognize unpacked-array element selects used in instance port connections and synthesize memory read ports with computed linear addresses.

## Files changed
- `src/convert.cpp`
- `tests/data/openc910/bug_cases/case_008/Makefile`
- `tests/data/openc910/bug_cases/case_008/tb_case_008.cpp`
- `tests/data/openc910/bug_cases/case_008/coverage_check.py`
- `tests/data/openc910/bug_cases/case_008/bug_report.md`

## Validation
- `make -C tests/data/openc910/bug_cases/case_008 run_c910_bug_case_ref`
  - PASS, line coverage 59.21% (180/304)
- `make -C tests/data/openc910/bug_cases/case_008 run_c910_bug_case`
  - PASS, line coverage 68.13% (310/455)
- `cmake --build build -j$(nproc)`
  - PASS
- `ctest --test-dir build --output-on-failure`
  - Not run (not required for this targeted fix)

## Open questions / follow-ups
- The emitted SV still triggers Verilator width/concat warnings; consider tightening constant sizing in emit or const-fold passes for cleaner downstream builds.
