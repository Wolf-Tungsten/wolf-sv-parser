# XS Bugcase 004: Sized-Cast Width Collapse

## Summary
This bugcase isolates a width/resize lowering error where a sized cast like `9'(...)` is effectively reduced to LSB-only, causing a compare against `9'd224` to fail.

## Root Cause
The RHS of `bad` is lowered under the assignment width context (1 bit). For explicit conversions, the lowerer reused that context when lowering the conversion operand, so the inner arithmetic was truncated to 1 bit *before* the explicit `9'(...)` cast was applied. The cast then zero-extended the already-truncated 1-bit value, producing `{8'b0, lsb}` and making the comparison fail.

Fix: when lowering an explicit conversion expression, reset the width context while lowering the operand so the cast width is preserved. This change lives in `src/convert.cpp` under the `ConversionExpression` handling.

## Repro
```
make -C tests/data/xs-bugcase/CASE_004 run
```
Coverage is disabled by default in this bugcase. To enable it:
```
make -C tests/data/xs-bugcase/CASE_004 run COVERAGE=1
```

## Expected Behavior
- Ref model: `sum = 224`, `bad = 0`.
- Wolf emit (pre-fix): `sum = 224`, `bad = 1`, triggering a mismatch.
- Wolf emit (post-fix): `sum = 224`, `bad = 0`, matches the ref model.

## Testbench Inputs
The stimulus is fixed in `tests/data/xs-bugcase/CASE_004/tb.cpp`:
- Reset: `rst_n=0` for 2 full cycles, then `rst_n=1`.
- Signals after reset deassert:
  - `flag_a = 1'b0`
  - `flag_b = 1'b0`
  - `val_a = 8'hDF`
  - `val_b = 8'h00`
  - `b0 = 5'h01`
  - `b1 = 5'h00`
  - `b2 = 5'h00`
- Expected: `sum = 9'd224`, `bad = 1'b0`.

## RTL Under Test
`tests/data/xs-bugcase/CASE_004/rtl/ExprCastBug.sv`

Inputs are chosen to mirror the MEFreeList formula:
- `flag_a/flag_b` and `val_a/val_b` approximate the `distanceBetween` path.
- `b0/b1/b2` approximate the nested popcount sum.

## Verilog Design (Input RTL)
The DUT is a combinational module that computes a distance-like value and adds
a nested sum, then checks the result against `9'd224`.

```verilog
wire [7:0] dist_val = (flag_a == flag_b)
    ? 8'(val_a - val_b)
    : 8'(8'(val_a - 8'h20) - val_b);

wire [5:0] inner = 6'({1'b0, b0} + {1'b0, 5'({1'b0, b1} + {1'b0, b2})});
wire [8:0] sum_cast = 9'({1'b0, dist_val} + {3'b0, inner});
assign sum = sum_cast;
assign bad = (9'({1'b0, dist_val} + {3'b0, inner}) != 9'd224);
```

## Notes
The expression mirrors the MEFreeList pattern:
```
9'({1'b0, a} + {3'b0, 6'({1'b0, b})})
```
### Before Fix (pre-fix wolf emit)
- The explicit `9'(...)` in the `bad` compare was lowered as if it were under a 1-bit context, so only the LSB of the arithmetic survived.
- The compare effectively became: `{8'b0, lsb} != 9'd224`, which is always true for this stimulus.

**Pre-fix wolf emit (simplified):**
```verilog
assign __expr_22 = __expr_17[0];
assign __expr_24 = __expr_18[0];
assign __expr_25 = __expr_22 + __expr_24;
assign __expr_27 = {8'b0, __expr_25};
assign __expr_28 = __expr_27 != 9'd224;
assign bad = __expr_28;
```

### After Fix (current wolf emit)
- The explicit cast operand is lowered without the outer width context, so the full 9-bit arithmetic is preserved.
- The compare now uses the full `9'(...)` value and matches the ref model.

**Post-fix wolf emit (simplified):**
```verilog
assign __expr_24 = __expr_17 + __expr_18;
assign __expr_25 = __expr_24 != 9'd224;
assign bad = __expr_25;
```
