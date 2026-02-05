# Case 022 Fix Report - Spurious NOLATCH from ct_rtu_rob_entry create mux

## Problem Summary

The wolf-sv-parser emitted an `always_latch` block for the create-data case mux in `ct_rtu_rob_entry`, which Verilator flagged with:

```
%Warning-NOLATCH: .../wolf_emit.sv:587184:3: No latches detected in always_latch block
587184 |   always_latch begin
       |   ^~~~~~~~~~~~
```

The source RTL uses a combinational `case` with a default assignment. Wolf’s write-back planning treated the full-width part-select writes as “sliced” and therefore failed to mark them as unconditional even though the case covers all values. This pushed the write-back into `kLatch` and generated `always_latch`.

## Minimal Repro

- **Case Path**: `tests/data/openc910/bug_cases/case_022/`
- **DUT Top**: `ct_rtu_rob_entry`
- **Coverage**: 97.16% (RTL), 98.70% (wolf)

### Repro Commands

```bash
make -C tests/data/openc910/bug_cases/case_022 run
make -C tests/data/openc910/bug_cases/case_022 run_c910_bug_case_ref
make -C tests/data/openc910/bug_cases/case_022 run_c910_bug_case
```

## Root Cause Analysis

Write-back planning tracks unconditional writes via `coversAllTwoState`, but only for full-signal (non-sliced) writes. The create-data case assigns `x_create_data[ROB_WIDTH-1:0]`, which was lowered as a slice even though it spans the full width. Because of that:

- `hasUnconditional` stayed false
- `usesTarget` stayed true (mux chain kept a base value)
- The domain was downgraded to `Latch`, producing `always_latch`

## Fix Details

### Files Changed

- `src/convert.cpp`

### Implementation

- Track when all writes are `coversAllTwoState` and the slice is a **full-width static slice**.
- Treat such writes as unconditional for domain selection.
- Allow `zeroBaseForSlices` in this case to avoid self-referential base values.

This keeps the create-data mux combinational and prevents `always_latch` emission.

## Validation Results

```bash
cmake --build build -j$(nproc)

make -C tests/data/openc910/bug_cases/case_022 run_c910_bug_case_ref
# [COVERAGE] line coverage 97.16% (205/211)

make -C tests/data/openc910/bug_cases/case_022 run_c910_bug_case
# [COVERAGE] line coverage 98.70% (228/231)
# No NOLATCH warning observed
```

## Risks and Limitations

- The change only applies when **all writes** are marked `coversAllTwoState` and the slice is a full-width static slice. Partial slices or mixed coverage paths are unaffected.
- If `coversAllTwoState` is over-approximated elsewhere, this could classify some latches as combinational; current checks are intentionally narrow to reduce risk.

## Open Questions / Follow-ups

- Should we generalize the unconditional-detection logic to handle multiple full-width slices or other coverage patterns beyond `coversAllTwoState`?

## Commands Executed

```bash
cmake --build build -j$(nproc)
make -C tests/data/openc910/bug_cases/case_022 run_c910_bug_case_ref
make -C tests/data/openc910/bug_cases/case_022 run_c910_bug_case
```
