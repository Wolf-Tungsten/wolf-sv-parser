# C910 Case 001 Fix Report

## Problem summary

`wolf-sv-parser` aborted when converting `ct_piu_top` due to multiple sequential always blocks assigning disjoint slices of `pkb_data`. The failure signature was:

```
Value already has a defining operation; value_180 (pkb_data) w=512 unsigned @../../C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1945:6; new_def=op_1171 kind=kRegister (pkb_data__register_0) @../../C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1953:6; existing_def=op_1151 kind=kRegister (pkb_data__register) @../../C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1945:6
```

Original log: `build/artifacts/c910_run.log`

## Minimal repro

- Case: `tests/data/openc910/bug_cases/case_001`
- DUT top: `ct_piu_top`
- RTL: `ct_piu_top.v` + `ct_fifo.v` + `ct_prio.v` + `gated_clk_cell.v` + `cpu_cfig.h`
- Coverage (RTL ref): 94.93% (786/828)

## Root cause analysis

The write-back lowering creates one `kRegister` op per sequential write-back entry. In `ct_piu_top`, `pkb_data` is a 512-bit register updated by four always blocks, each writing a distinct 128-bit slice with its own gated clock. The graph builder enforces a single defining op per value, so the second `kRegister` targeting the same `pkb_data` value triggers the runtime error.

## Fix details

### Approach

- Detect sequential write-back groups where all writes target the same static slice range.
- When multiple such slice-register entries exist for the same symbol and fully cover the target width, emit separate slice registers and drive the full target via a concatenation of those slice registers.
- This avoids multiple defining ops on the same value while preserving slice semantics.

### Code changes

- `include/convert.hpp`: add static-slice metadata to `WriteBackPlan::Entry`.
- `src/convert.cpp`:
  - Capture static slice ranges for write-back groups with consistent slices (even when multiple writes exist due to reset + data assignments).
  - In `emitWriteBack`, detect multiple sequential static-slice entries that cover the full width, emit per-slice `kRegister` ops, then `kConcat` into the original target value.

## Validation

- Reference RTL run:
  - `make -C tests/data/openc910/bug_cases/case_001 run_c910_bug_case_ref`
  - Coverage: 94.93% (786/828)
- Wolf emit run:
  - `make -C tests/data/openc910/bug_cases/case_001 run_c910_bug_case`
  - Result: completes successfully; coverage 98.99% (2066/2087)
- Full test suite:
  - `ctest --test-dir build --output-on-failure`

## Risks and follow-ups

- The slice-register concat path currently requires full-width, contiguous static slices; non-covering or overlapping slices still fall back to the prior behavior.
- Emitted SV now represents slice registers + concat for these patterns; verify downstream tools tolerate this representation.
- The run emitted existing converter TODO/warn diagnostics (memory read indices, ignored event lists) that are unrelated to this fix.
