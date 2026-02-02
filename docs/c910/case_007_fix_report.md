# case_007 Fix Report

## Problem summary
- Source log: `build/artifacts/c910_run.log`
- Failure signature:

```
[convert] [ERROR] ../logical/gpio/gpio.v:160:8 - Unsupported inout slice assignment
  statement: assign b_pad_gpio_porta[0] = gpio_porta_oe[0] ? gpio_porta_dr[0] : 1'bz;
```

## Minimal repro
- Case path: `tests/data/openc910/bug_cases/case_007`
- DUT top: `gpio`
- RTL: `tests/data/openc910/smart_run/logical/gpio/{gpio,gpio_apbif,gpio_ctrl}.v`
- Ref coverage: 97.82% (403/412)

## Root cause
- `handleInoutAssignment` rejected any inout slice assignment, preventing conversion of
  `b_pad_gpio_porta[0] = ...`.
- When enabling slice writes, write-back couldn’t resolve a base width for the synthetic
  inout binding signals (`__out`/`__oe`), causing `Write-back slice missing target width`.
- Range select lowering applied packed-index adjustment twice, generating invalid
  slices for packed arrays with non-zero LSB (e.g., `paddr[6:2]`).

## Fix
- Allow inout slice assignments and propagate slices to the generated `__out`/`__oe` writes.
- Seed write-back width/type maps for synthetic inout binding symbols based on the
  parent inout port.
- Remove the duplicate packed-index adjustment in range select lowering.

Files touched:
- `src/convert.cpp`

## Validation
- `make -C tests/data/openc910/bug_cases/case_007 run_c910_bug_case_ref`
  - PASS, coverage 97.82%.
- `make -C tests/data/openc910/bug_cases/case_007 run_c910_bug_case`
  - PASS, conversion + TB pass; coverage reported (95.52%).

## Notes / follow-ups
- Wolf emit still reports warnings about ignored timing controls and width truncation
  for inout OE; these are pre-existing limitations but do not affect this repro.
