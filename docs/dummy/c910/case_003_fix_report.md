# case_003 fix report - const-fold parse failure on abbreviated decimals

## Problem summary
- wolf-sv-parser fails during const-fold when parsing `constValue` attributes that include abbreviated decimal strings (with `.` / `e`), e.g.:
  - `[transform] [const-fold] [error] Failed to parse constValue '258'd62771017353866807634955070562867279526...e20': Digit 254 too large for radix 10 (ct_iu_div_entry::)`
- Original log: `build/artifacts/c910_run.log`

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_003`
- DUT top: `ct_iu_div_entry`
- Coverage: 93.65% line coverage for RTL, 93.18% for wolf_emit
- Repro commands:
  - `make -C tests/data/openc910/bug_cases/case_003 run`
  - `make -C tests/data/openc910/bug_cases/case_003 run_c910_bug_case_ref`
  - `make -C tests/data/openc910/bug_cases/case_003 run_c910_bug_case`

## Root cause analysis
- `convert.cpp` used `slang::SVInt::toString()` with the default abbreviation threshold for constant literals.
- For large signed decimal values, `SVInt::toString()` emits abbreviated scientific notation (e.g., `...e20`) which includes `.` and `e`.
- `const_fold` later re-parses `constValue` via `SVInt::fromString()`, which does not accept abbreviated decimal notation and throws on the `.` character (reported as digit 254 too large for radix 10).

## Fix details
- Added `formatIntegerLiteral()` in `src/convert.cpp` to call `SVInt::toString(SVInt::MAX_BITS)`.
- Replaced all constant literal serialization sites to use the non-abbreviating formatter for:
  - integer literals
  - evaluated constant expressions
  - parameter-derived integer constants
- This preserves the base selection logic while preventing scientific notation output.

## Files changed
- `src/convert.cpp`
- `tests/data/openc910/bug_cases/case_003/Makefile`
- `tests/data/openc910/bug_cases/case_003/filelist.f`
- `tests/data/openc910/bug_cases/case_003/tb_case_003.cpp`
- `tests/data/openc910/bug_cases/case_003/bug_report.md`
- `tests/data/openc910/bug_cases/case_003/coverage_check.py`

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_003 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_003 run_c910_bug_case`
- `ctest --test-dir build --output-on-failure`

## Open questions / follow-ups
- None identified. If output size becomes a concern, consider using a fixed base (hex) for large constants while keeping the non-abbreviated setting.
