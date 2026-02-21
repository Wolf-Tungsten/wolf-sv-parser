# C910 Case 002 Fix Report

## Problem summary

`wolf-sv-parser` aborted when converting `ct_ciu_ncq` due to multiple always blocks assigning the loop index variable `i`. The failure signature was:

```
Value already has a defining operation; value_324 (i) w=32 signed @../../C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1437:9; new_def=op_1674 kind=kRegister (i__register_0) @../../C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1458:9; existing_def=op_987 kind=kRegister (i__register) @../../C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1437:9
```

Original log: `build/artifacts/c910_run.log`

## Minimal repro

- Case: `tests/data/openc910/bug_cases/case_002`
- DUT top: `ct_ciu_ncq`
- RTL: `ct_ciu_ncq.v` + `ct_ciu_ncq_gm.v` + `ct_fifo.v` + `ct_prio.v` + `gated_clk_cell.v`
- Coverage (RTL ref): 95.38% (640/671)

## Root cause analysis

`scanForLoopControl` traversed loop init/step expressions using `AssignmentExprVisitor`, which calls `handleAssignment` and records write intents. For `for (i=0; i<...; i=i+1)` loops, this created sequential write-back entries for the loop index variable `i` (declared at module scope), even though the loop variable should be treated as a compile-time local. Multiple always blocks in `ct_ciu_ncq` each contain loops using `i`, so the graph builder saw multiple `kRegister` defining ops for the same value and threw the runtime error.

## Fix details

### Approach

- Suppress write-intent recording while scanning loop control expressions.
- Skip lowering assignments that target loop-local symbols during loop lowering.

### Code changes

- `src/convert.cpp`:
  - Add `suppressAssignmentWrites` and an `AssignmentSuppressScope` to disable write intents during loop control scanning.
  - Update `AssignmentExprVisitor` to honor the suppression flag.
  - In `handleAssignment`, return early when the LHS resolves to a loop-local symbol.

## Validation

- Reference RTL run:
  - `make -C tests/data/openc910/bug_cases/case_002 run_c910_bug_case_ref`
  - Coverage: 95.38% (640/671)
- Wolf emit run:
  - `make -C tests/data/openc910/bug_cases/case_002 run_c910_bug_case`
  - Result: completes successfully; coverage 92.58% (1223/1321)
- Full test suite:
  - `ctest --test-dir build --output-on-failure`

## Risks and follow-ups

- Loop control assignments are now ignored during scan/lowering when they target loop-local symbols. If a design intentionally uses a loop index as architectural state outside the loop (non-synthesizable but possible), this behavior may differ from simulation.
- The run still emits existing converter TODO/warn diagnostics (memory read indices, ignored event lists) unrelated to this fix.
