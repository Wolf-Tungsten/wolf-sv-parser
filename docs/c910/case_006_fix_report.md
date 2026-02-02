# case_006 fix report - memory read index missing in convert

## Problem summary

`make run_c910_test` failed while converting `axi2ahb` due to `Memory read missing address indices` on an unpacked array read (`entry_data[rptr[2:0]]`). The failure blocked SV/JSON emission and the subsequent Verilator run.

## Failure signature

- Log: `build/artifacts/c910_run.log`
- Error snippet:

```
[convert] [ERROR] ../logical/axi/axi2ahb.v:627:7 - Memory read missing address indices
  statement: db_out_reg[127:0] <=   entry_data[rptr[2:0]];
```

## Minimal repro

- Case: `tests/data/openc910/bug_cases/case_006`
- DUT top: `axi2ahb`
- Filelist: `tests/data/openc910/smart_run/logical/axi/axi2ahb.v`
- Reference coverage: 97.75% line coverage (`run_c910_bug_case_ref`)
- Wolf coverage: 99.65% line coverage (`run_c910_bug_case`)

## Root cause

`MemoryPortLowererPass::visitExpr` records memory read candidates for every visited node. For unpacked array reads lowered as `kSliceDynamic`, the traversal also visited the base symbol node, which was interpreted as a memory read with no indices. That produced an empty `addressIndices` list for a memory that has unpacked dimensions, causing `buildLinearAddress` to fail with the reported error.

## Fix details

- Change: Skip recursing into the base operand of `kSliceDynamic` when the current node is already recognized as a memory slice read, preventing duplicate/partial memory read candidates.
- Files changed:
  - `src/convert.cpp`
- Rationale: The `kSliceDynamic` node already captures the full index chain; visiting its base symbol is redundant and can only produce incomplete address lists for memories.
- Risk: Low. The change only alters traversal for memory slices and leaves non-memory slices unchanged. Expressions with slices over non-memory values still recurse fully.

## Validation

- `make -C tests/data/openc910/bug_cases/case_006 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_006 run_c910_bug_case`
- `ctest --test-dir build --output-on-failure`

Notes:
- Verilator emitted `UNOPTFLAT` warnings on the wolf-emitted SV for `axi2ahb` (circular combinational logic). The run still completed and produced coverage; warnings are preserved for review.

## Open questions / follow-ups

- None for this fix. If desired, we could add a targeted unit test under `convert-memory-port-lowerer` for unpacked array reads to guard against regressions.
