# case_023 Fix Report

## Problem summary
The C910 flow emitted `wolf_emit.sv` that triggered Verilator `UNOPTFLAT` circular combinational logic warnings in `ct_ifu_bht`, e.g. for `bht_pred_array_index` and related signals. The warnings were reproducible in the minimized case under `tests/data/openc910/bug_cases/case_023` using the emitted SV.

## Failure signature
- Log snippet (original):
  - `.../smart_run/work/wolf_emit/sim_top_wolf.sv` reporting `UNOPTFLAT` on `bht_pred_array_index` / `bht_pred_array_rd_index`.
- Minimized repro:
  - `.../build/c910_bug_case/case_023/wolf/wolf_emit.sv` reported `UNOPTFLAT` for `ct_ifu_bht.bht_pred_array_index` (and related expr signals) before the fix.

## Minimal repro
- Case path: `tests/data/openc910/bug_cases/case_023`
- DUT: `ct_ifu_bht`
- Coverage (ref run): 95.66% (1037/1084) via `run_c910_bug_case_ref`

## Root cause
The write-back slice lowering used the target signal as the base value for slice updates even when every write was a full-width static slice (e.g., `bht_pred_array_index[9:0] = ...`). For latch-domain assignments this introduced self-referential combinational expressions (e.g., `__expr_153 = cond ? new : bht_pred_array_index`), which Verilator reported as circular combinational logic (`UNOPTFLAT`).

## Fix
Treat full-width static slice assignments as not needing a base value. This forces the slice update path to use a zero base instead of the target signal, eliminating self-referential expressions for full-width updates.

- Updated: `src/convert.cpp`
- Change: set `zeroBaseForSlices = true` whenever `fullWidthStaticSlice` is true.

## Before/After example

Original RTL intent (full-width slice assignment):
```verilog
always @* begin
  bht_pred_array_index[9:0] = sel ? a[9:0] : b[9:0];
end
```

Before (emitted SV introduced a self-reference):
```verilog
assign __expr_153 = sel ? a : bht_pred_array_index;
assign __expr_154 = sel2 ? b : __expr_153;
always_latch begin
  if (cond) begin
    bht_pred_array_index = __expr_154;
  end
end
```

After (emitted SV uses the new value directly):
```verilog
assign __expr_153 = sel ? a : b;
always_latch begin
  if (cond) begin
    bht_pred_array_index = __expr_153;
  end
end
```

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_023 run_c910_bug_case_ref`
  - Coverage 95.66% (1037/1084)
- `make -C tests/data/openc910/bug_cases/case_023 run_c910_bug_case`
  - No `UNOPTFLAT` warnings observed; coverage 97.61% (777/796)

## Notes / follow-ups
- The converter still warns about ignored timing controls in several `always @(signal)` blocks; those are pre-existing and unaffected by this fix.
- `ctest --test-dir build --output-on-failure` was not run in this session.
