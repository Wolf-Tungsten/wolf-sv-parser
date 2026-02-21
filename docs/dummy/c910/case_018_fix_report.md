# case_018 Fix Report

## Problem summary
- Wolf-emitted SV for `ct_idu_is_lsiq_entry` truncated vector operands in `&(x_other_*[10:0] & agevec_all[10:0] & ...)` down to 1-bit because the assignment width context forced bitwise ops to LHS width.
- This produced Verilator `WIDTHEXPAND/WIDTHTRUNC` warnings when the resulting 1-bit term was ANDed with an 11-bit vector.

## Failure signature
```
%Warning-WIDTHEXPAND: .../wolf_emit.sv:2081:34: Operator AND expects 11 bits on the LHS, but LHS's VARREF '__expr_399' generates 1 bits.
  assign __expr_402 = __expr_399 & __expr_401;
%Warning-WIDTHTRUNC: .../wolf_emit.sv:2081:21: Operator ASSIGNW expects 1 bits on the Assign RHS, but Assign RHS's AND generates 11 bits.
  assign __expr_402 = __expr_399 & __expr_401;
```

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_018`
- DUT top: `ct_idu_is_lsiq_entry`
- RTL sources:
  - `../../C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_is_lsiq_entry.v`
  - `../../C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_dep_reg_entry.v`
  - `../../C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_dep_vreg_entry.v`
  - `../../C910_RTL_FACTORY/gen_rtl/clk/rtl/gated_clk_cell.v`
- Ref coverage: 96.47% (656/680)
- Wolf coverage: 96.20% (582/605)

## Root cause analysis
- `handleAssignment` sets a width context equal to the LHS width when lowering RHS expressions.
- In `lowerExpression` for bitwise operators, `widthContext` previously forced operands to resize to the LHS width, even when operand widths were already known.
- For `assign no_spec_check_wakeup = load && !(|(x_other_store[10:0] & x_other_no_spec[10:0] & agevec_all[10:0] & ~x_other_frz[10:0]));`, the LHS is 1-bit, so the bitwise `&` chain was truncated to 1-bit before reduction, leaving a 1-bit term ANDed with an 11-bit vector and triggering the warnings.

## Fix details
- Updated `lowerExpression` sizing for **bitwise** binary ops to prefer operand widths when known and only fall back to `widthContext` if both widths are unknown.
- Kept **arithmetic** ops using `widthContext` when available to avoid unintended index-width growth in part-selects (e.g. HDLBits 065).
- This preserves full-width bitwise operations and relies on the final assignment resize to match the LHS width.
- Files changed: `src/convert.cpp`.
- Added repro case: `tests/data/openc910/bug_cases/case_018`.

## Example walkthrough

### Source expression (from RTL)
```
assign no_spec_check_wakeup =
  load && !(|(x_other_store[10:0] & x_other_no_spec[10:0]
               & agevec_all[10:0] & ~x_other_frz[10:0]));
```

### What went wrong before
- This is a 1‑bit assign (`no_spec_check_wakeup`), so the assignment handler set `widthContext = 1`.
- The bitwise `&` chain operands have **known width 11**, but the old logic **forced them to widthContext** whenever it existed.
- Result: the IR became effectively `((11b & 11b & 11b & 11b) -> 1b)` *before* the reduction `|`, which is not what the RTL says.
- When emitting SV, the 1‑bit term was ANDed with an 11‑bit vector in the next step, leading to:
  - `WIDTHEXPAND` (1‑bit vs 11‑bit)
  - `WIDTHTRUNC` (assigning 11‑bit AND to 1‑bit wire)

### What happens after the fix
- New rule: **bitwise** ops prefer operand widths when known and only fall back to `widthContext` if both sides are unknown.
- **Arithmetic** ops continue to honor `widthContext` when present, to avoid index-width growth in part-selects.
- In this case, each bitwise operand is 11‑bit, so the `&` chain stays **11‑bit**, and the reduction `|(...)` correctly reduces to 1‑bit before the final assign.

## Related regression note (HDLBits 065)

### Source expression
```
assign out = in[4*sel + 3 -: 4];
```

### Regression observed
- When arithmetic ops were also forced to use operand widths, the index math (`4*sel + 3`) expanded to 64‑bit.
- Verilator then reported `WIDTHEXPAND/WIDTHTRUNC` for the generated subtract/add inside the index calculation.

### Resolution
- Keep `widthContext` for **arithmetic** so index expressions stay at the intended width (32‑bit here).
- Retain the **bitwise** fix so vector `&` chains are not truncated before reductions.

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_018 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_018 run_c910_bug_case`
- `ctest --test-dir build --output-on-failure`
- Confirmed `WIDTHEXPAND/WIDTHTRUNC` warnings are gone in wolf emit.

## Follow-ups / risks
- This change alters sizing for bitwise/arithmetic ops under width contexts; monitor for any edge cases where both operand widths are unknown.
