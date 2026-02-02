# case_010 fix report

## Problem summary
- C910 convert failed with: "Skipping instance with missing port connection" on input `corec_pmu_sleep_out`.
- Failure signature (from `build/artifacts/c910_run.log`):

```
[convert] [ERROR] ../logical/pmu/pmu.v:56:17 - Skipping instance with missing port connection
  statement: input           corec_pmu_sleep_out;
```

## Minimal repro
- Case path: `tests/data/openc910/bug_cases/case_010`
- DUT top: `case_010_top`
- RTL: `case_010_top.v` + `pmu_min.v`
- Coverage (ref): 91.67% (COV_MIN 90)

## Root cause analysis
- `emitInstances()` treated missing input port connections as fatal errors.
- For `.port()` syntax, Slang returns a null expression, but this is legal Verilog meaning the input is left unconnected.
- The converter rejected this instead of emitting an unconnected/unknown input value.

## Fix details
- Accept unconnected input ports by emitting a width-matched constant `x` value and warning instead of erroring.
- This mirrors existing behavior for unconnected outputs while preserving conversion flow.
- Files changed:
  - `src/convert.cpp`
  - `tests/data/openc910/bug_cases/case_010/*`

## Validation
- `make -C tests/data/openc910/bug_cases/case_010 run_c910_bug_case_ref`
  - Result: pass; coverage 91.67%.
- `make -C tests/data/openc910/bug_cases/case_010 run_c910_bug_case`
  - Result: pass; coverage 90.00%; converter now warns about the unconnected input instead of failing.
- `ctest --test-dir build --output-on-failure`
  - Result: pass (23 tests).

## Follow-ups
- Consider whether unconnected inout ports should also be downgraded from error to warning.
