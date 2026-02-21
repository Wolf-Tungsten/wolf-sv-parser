# case_005 Fix Report

## Problem summary
- `case_005` tri-state regression: `pad_sample` expected `0xA5` after first tick but got `0x00` in the wolf flow.
- `wolf_emit.sv` showed `b_pad_gpio_porta__oe` undriven and the inout assign rewritten into `__out`, which masked the intended tri-state behavior.

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_005`
- DUT top: `sim_top`
- Coverage (ref): 100.00% line coverage (49/49)
- Repro commands:
  - `make -C tests/data/openc910/bug_cases/case_005 run`
  - `make -C tests/data/openc910/bug_cases/case_005 run_c910_bug_case_ref`
  - `make -C tests/data/openc910/bug_cases/case_005 run_c910_bug_case`

## Root cause analysis
- The canonical tri-state assign (`assign inout = oe ? out : {N{1'bz}};`) was not recognized as a tri-state wiring form during lowering.
- The RHS arrived wrapped in conversions, so Z-branch detection failed and the fallback path wrote:
  - `__out = (oe ? out : Z)` and `__oe = 1`, which overrode the real procedural drivers.
- As a result, `__oe` had no valid driver and the inout port never presented high‑Z in the wolf flow.

## Fix details
- (From earlier case_005 work) Added inout signal bindings for nets connected to inout ports and reordered instance results so outputs precede inout inputs.
- Improved Z detection for RHS branches (replication/concat, literals, and explicit conversions).
- Added a canonical tri-state detector: if the conditional uses the binding’s `__oe` and `__out` and the other branch contains a Z literal, the assign is treated as wiring-only and skipped.
- This preserves the real procedural drivers for `__out/__oe` and prevents the fallback write from masking tri‑state behavior.

### Files changed
- `src/convert.cpp`
  - Added robust Z‑literal detection for tri‑state assigns.
  - Skipped canonical inout wiring assigns to avoid overriding `__out/__oe`.

## Validation
- `make -C tests/data/openc910/bug_cases/case_005 run` (pass)
- `make -C tests/data/openc910/bug_cases/case_005 run_c910_bug_case_ref` (pass; 100.00% coverage)
- `make -C tests/data/openc910/bug_cases/case_005 run_c910_bug_case` (pass; 94.12% coverage)
- `make run_c910_test` still fails in the full C910 flow (unrelated `plic_hreg_busif` error; see `build/artifacts/c910_run.log`).

## Risks / follow-ups
- Canonical detection relies on a Z‑literal in the non‑drive branch. Unusual encodings of high‑Z may still bypass the shortcut and fall back to direct rewriting.
- Verilator still warns about 8‑bit conditional tests for `__oe` in the generated SV; consider optional reduction or lint suppression if noisy.
