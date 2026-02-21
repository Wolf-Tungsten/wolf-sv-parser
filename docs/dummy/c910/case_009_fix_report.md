# case_009 fix report

## Problem summary
- C910 convert failed with: "Skipping instance with mismatched output slice width" on `plic_hart_arb` output `arbx_hreg_claim_id`.
- Failure signature (from `build/artifacts/c910_run.log`):

```
[convert] [ERROR] ../../C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hart_arb.v:65:33 - Skipping instance with mismatched output slice width
  statement: output  [ID_NUM-1   :0]         arbx_hreg_claim_id;
```

## Minimal repro
- Case path: `tests/data/openc910/bug_cases/case_009`
- DUT top: `case_009_top`
- RTL: `case_009_top.v` + `plic_hart_arb.v` + `stub_modules.v`
- Coverage (ref): 18.57% (COV_MIN set to 18)

## Root cause analysis
- `emitInstances()` resolves output bindings for `RangeSelectExpression` by treating all selections as simple ranges.
- For indexed part-selects (`+:` / `-:`), the RHS is the width, not the upper index. The old logic computed `width = hi - lo + 1`, yielding a mismatched width (e.g., `ID_NUM+:ID_NUM` produced width 1), which triggered the error.

## Fix details
- Added explicit handling for `RangeSelectionKind::IndexedUp` and `IndexedDown` when resolving output bindings.
  - `IndexedUp`: `low = left`, `width = right`
  - `IndexedDown`: `low = left - width + 1`, `width = right`
  - Reject non-positive widths.
- Files changed:
  - `src/convert.cpp`
  - `tests/data/openc910/bug_cases/case_009/*`

## Validation
- `make -C tests/data/openc910/bug_cases/case_009 run`
  - Result: success; convert error no longer reproduces.
- `make -C tests/data/openc910/bug_cases/case_009 run_c910_bug_case_ref`
  - Result: pass; coverage 18.57% >= 18.
- `make -C tests/data/openc910/bug_cases/case_009 run_c910_bug_case`
  - Result: pass; coverage 20.34%; warnings observed from Verilator (UNOPTFLAT) and convert (overlapping slice merges) but no failures.

## Follow-ups
- Consider whether overlapping slice merge warnings in `plic_hart_arb` should be addressed or suppressed.
- Decide if a different coverage metric is preferred for structural RTL with many declaration lines.
