# case_013 Fix Report

## Problem summary
`make run_c910_test` failed during convert with an unsupported unary operator in the C910 VFMau mult block. The failure comes from a unary minus applied to a literal in a comparison.

- Log: build/artifacts/c910_run.log
- Failure signature:
  ```
  [convert] [ERROR] ../../C910_RTL_FACTORY/gen_rtl/vfmau/rtl/ct_vfmau_mult1.v:1267:78 - Unsupported unary operator
    statement: || ($signed(mult1_ex2_expnt_diff[12:0]) > -2);
  ```

## Minimal repro
- Case: tests/data/openc910/bug_cases/case_013
- DUT top: ct_vfmau_mult1
- Ref coverage: 91.15% (2483/2724) via `run_c910_bug_case_ref`
- Wolf coverage: 96.10% (2565/2669) via `run_c910_bug_case` (COV_MIN=0 for wolf run)

## Root cause analysis
`convert.cpp` only mapped unary operators that are logical/bitwise reductions. Unary plus/minus were unhandled, so a literal like `-2` is parsed as a unary minus expression and rejected with “Unsupported unary operator.”

## Fix details
- Added explicit handling for `UnaryOperator::Plus` and `UnaryOperator::Minus` in both expression lowerers:
  - Unary plus is treated as a no-op (with optional width-resize under width context).
  - Unary minus is lowered to a subtraction node (`0 - operand`) with width hints preserved.
- Files changed:
  - src/convert.cpp

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_013 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_013 run_c910_bug_case`
- `ctest --test-dir build --output-on-failure`

## Open questions / follow-ups
- None. (Warnings from Verilator about unoptimized combinational loops remain as before and are not part of this fix.)
