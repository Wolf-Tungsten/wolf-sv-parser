# case_019 Fix Report

## Problem summary
A WIDTHTRUNC warning was reported in wolf-emitted SV where an intermediate temp was inferred as 1-bit even though the RHS was a vector, e.g.:

```
%Warning-WIDTHTRUNC: ... sim_top_wolf.sv:361189:21: Operator ASSIGNW expects 1 bits on the Assign RHS, but Assign RHS's AND generates 8 bits.
361189 |   assign __expr_811 = __expr_808 & __expr_810;
```

The warning originates in the C910 flow (ct_lsu_rb), but the same reduction pattern exists in smaller RTL, allowing a minimal repro.

## Minimal repro
- Case path: `tests/data/openc910/bug_cases/case_019`
- DUT top: `ct_l2c_data`
- Kept RTL: `ct_l2c_data`, `gated_clk_cell`
- Stub: `stub_modules.v` defines `L2C_DATA_INDEX_WIDTH=13`
- Coverage (ref): 97.47% line coverage (193/198)
- Coverage (wolf): 98.43% line coverage (188/191)

## Root cause
In `src/convert.cpp`, unary operator lowering did not apply a width hint for non-reduction unary ops (notably bitwise NOT). When such expressions were lowered under a 1-bit width context (assignment to a 1-bit LHS), the intermediate result width remained unknown and the downstream bitwise ops adopted the 1-bit context. This produced 1-bit temporaries assigned from vector RHS values, triggering WIDTHTRUNC in the emitted SV.

## Fix
Apply `applyExprWidthHint` to unary ops when a width hint was not already set (non-reduction cases). This preserves operand widths for bitwise NOT (and keeps logical-not as 1-bit via the computed type), preventing spurious truncation.

- File: `src/convert.cpp`
- Change: set width hint for unary ops when unset

## Example (before/after)
Before fix (wolf-emitted SV had 1-bit temp from vector RHS):

```
assign __expr_113 = ~__expr_112; // __expr_112 is [4:0], __expr_113 inferred as 1-bit
assign l2c_data_ram_clk_en_x = |__expr_113;
```

After fix (unary result keeps operand width):

```
assign __expr_113 = ~__expr_112; // __expr_113 is [4:0]
assign l2c_data_ram_clk_en_x = |__expr_113;
```

## Validation
- `make -C tests/data/openc910/bug_cases/case_019 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_019 run_c910_bug_case`
- `ctest --test-dir build --output-on-failure`

## Open questions / follow-ups
- None. The minimal repro no longer emits WIDTHTRUNC after the fix; the original ct_lsu_rb case should be rechecked in the full C910 flow if needed.
