# XS Bugcase 002: DPI return type + sequential timing mismatch

## Summary

`make -C tests/data/xs-bugcase/CASE_002 run` initially failed for two reasons:

1) Verilator rejected `wolf_emit.sv` because DPI imports returned 4-state
   vectors (`logic [N:0]`), which is illegal per IEEE 1800-2023 and Verilator.
2) After fixing the DPI type, the ref vs wolf simulation mismatched due to
   DPI return values being used one delta-cycle late in sequential logic.

This bugcase preserves DPI types, fixes DPI result usage inside
`always @(posedge ...)`, and makes the testbench deterministic for `jtag_tick`.

## Reproduction

```
make -C tests/data/xs-bugcase/CASE_002 run
```

Failure 1 (Verilator):

```
%Error: .../wolf_emit.sv:76:47: DPI function may not return a 4-state type other than a single 'logic'
  import "DPI-C" function logic signed [63:0] difftest_ram_read (...)
```

Failure 2 (after fixing DPI types, sim mismatch):

```
[MISMATCH] cycle=3 exit ref=1 wolf=0
```

## Analysis

### 1) DPI return type is wrong (4‑state vs 2‑state integer)

**Source expectation (RTL, CASE_002):**

```
// tests/data/xs-bugcase/CASE_002/rtl/Mem1R1WHelper.v
`ifndef DISABLE_DIFFTEST_RAM_DPIC
import "DPI-C" function longint difftest_ram_read(input longint rIdx);
`endif

// tests/data/xs-bugcase/CASE_002/rtl/SimJTAG.v
import "DPI-C" function int jtag_tick
(
 output bit jtag_TCK,
 output bit jtag_TMS,
 output bit jtag_TDI,
 output bit jtag_TRSTn,
 input bit  jtag_TDO
);
```

**Before (wolf_emit.sv, CASE_002 pre-fix):**

```
import "DPI-C" function logic signed [63:0] difftest_ram_read (
  input longint rIdx
);
import "DPI-C" function logic signed [31:0] jtag_tick (
  output bit jtag_TCK,
  output bit jtag_TMS,
  output bit jtag_TDI,
  output bit jtag_TRSTn,
  input bit jtag_TDO
);
```

These are 4‑state vectors. Verilator (and IEEE 1800‑2023 35.5.5) forbids
4‑state DPI return types except a single `logic`, so compile fails.

**After (wolf_emit.sv, CASE_002 fixed):**

```
import "DPI-C" function longint difftest_ram_read (
  input longint rIdx
);
import "DPI-C" function int jtag_tick (
  output bit jtag_TCK,
  output bit jtag_TMS,
  output bit jtag_TDI,
  output bit jtag_TRSTn,
  input bit jtag_TDO
);
```

The converter now preserves `int/longint/bit/...` instead of collapsing to `logic`.

### 2) DPI return value used one delta late in sequential logic

**Source (tests/data/xs-bugcase/CASE_002/rtl/SimJTAG.v):**

```
always @(posedge clock) begin
  r_reset <= reset;
  if (reset || r_reset) begin
    __exit = 0;
    tickCounterReg <= TICK_DELAY;
    init_done_sticky <= 1'b0;
    __jtag_TCK = !__jtag_TCK;
  end else begin
    init_done_sticky <= init_done | init_done_sticky;
    if (enable && init_done_sticky) begin
      tickCounterReg <= tickCounterNxt;
      if (tickCounterReg == 0) begin
        __exit = jtag_tick(
          __jtag_TCK, __jtag_TMS, __jtag_TDI, __jtag_TRSTn, __jtag_TDO);
      end
    end
  end
end
```

**Before (wolf_emit.sv, CASE_002 pre-fix):**

```
assign _dpi_ret_4 = _dpi_ret_4_intm;
assign __expr_13 = __expr_5 ? _dpi_ret_4 : 32'd0;

always @(posedge clock) begin
  if (__expr_5) begin
    _dpi_ret_4_intm = jtag_tick(_dpi_ret_0_intm, _dpi_ret_1_intm,
                               _dpi_ret_2_intm, _dpi_ret_3_intm, __jtag_TDO);
  end
  if (__expr_12) begin
    __exit <= __expr_13;
  end
end
```

`_dpi_ret_4_intm` updates inside the `always` block, but `__expr_13` is computed
from the *wire* `_dpi_ret_4`. That wire is updated by a continuous assignment,
so in this time step it still holds the previous value. The exit register
lags by one delta cycle.

**After (wolf_emit.sv, CASE_002 fixed):**

```
always @(posedge clock) begin
  if (__expr_5) begin
    _dpi_ret_4_intm = jtag_tick(_dpi_ret_0_intm, _dpi_ret_1_intm,
                               _dpi_ret_2_intm, _dpi_ret_3_intm, __jtag_TDO);
  end
  if (__expr_12) begin
    __exit <= __expr_5 ? _dpi_ret_4_intm : 32'd0;
  end
end
```

We inline DPI‑dependent expressions in sequential logic, so the same clock edge
uses `_intm` directly.

### 3) Testbench DPI state was shared across ref/wolf models

**Before (tests/data/xs-bugcase/CASE_002/tb.cpp, pre-fix):**

```
extern "C" int jtag_tick(svBit *jtag_TCK,
                          svBit *jtag_TMS,
                          svBit *jtag_TDI,
                          svBit *jtag_TRSTn,
                          svBit jtag_TDO) {
    static int tick_count = 0; // shared by both models
    const int value = tick_count ^ (jtag_TDO ? 1 : 0);
    *jtag_TCK = (tick_count >> 0) & 1;
    *jtag_TMS = (tick_count >> 1) & 1;
    *jtag_TDI = (tick_count >> 2) & 1;
    *jtag_TRSTn = (tick_count >> 3) & 1;
    ++tick_count;
    return value;
}
```

The DPI function is called by both VRef and VWolf; they were incrementing the
same counter, so outputs diverged.

**After (tests/data/xs-bugcase/CASE_002/tb.cpp):**

```
extern "C" int jtag_tick(svBit *jtag_TCK,
                          svBit *jtag_TMS,
                          svBit *jtag_TDI,
                          svBit *jtag_TRSTn,
                          svBit jtag_TDO) {
    static int tick_counts[2] = {0, 0};
    extern int g_model_index;
    int &tick_count = tick_counts[g_model_index];
    const int value = tick_count ^ (jtag_TDO ? 1 : 0);
    *jtag_TCK = (tick_count >> 0) & 1;
    *jtag_TMS = (tick_count >> 1) & 1;
    *jtag_TDI = (tick_count >> 2) & 1;
    *jtag_TRSTn = (tick_count >> 3) & 1;
    ++tick_count;
    return value;
}

static void tick(VRef *ref, VWolf *wolf, bool clk) {
    ref->clk = clk;
    wolf->clk = clk;
    g_model_index = 0;
    ref->eval();
    g_model_index = 1;
    wolf->eval();
    ++main_time;
}
```

Each model gets its own counter, keeping behavior aligned.

## Fix (what changed)

- Preserve DPI integer/bit/real/chandle/string types in conversion and emit
  them explicitly in SystemVerilog (`int`, `longint`, `bit`, etc). Also allow
  `bit [N:0]` returns (2-state), avoiding the 4-state DPI restriction.
- Inline DPI-dependent expressions in sequential logic so that any value that
  depends on a DPI call uses the `_intm` temporary in the same `always` block.
- Make `jtag_tick` deterministic per model by tracking a counter per model
  index in `tb.cpp` and selecting the index before each `eval()`.
- Use a writable `CCACHE_DIR` for this bugcase and link `verilated_dpi.cpp`
  so DPI helpers resolve cleanly in standalone builds.

## Emit logic (current behavior, DPI-dependent folding)

The emitter decides what gets folded into the same `always` block based on a
simple dependency analysis:

1) **Mark DPI results:** For every `kDpicCall` result, the emitter creates a
   module variable `<value>_intm` and records that ValueId in `dpiTempNames`.
   These `_intm` names are treated as the canonical DPI return holders.
2) **Dependency walk (`valueDependsOnDpi`)**: For any ValueId, walk its defining
   operation and operands. If the value itself is a DPI result or any operand
   depends on DPI (through a whitelist of pure ops like `kAdd`, `kMux`,
   `kSlice*`, `kConcat`, comparisons, reductions, etc.), the value is marked
   DPI-dependent. Unknown ops are treated as non-DPI (no folding).
3) **Sequential emission uses `valueExprSeq`:** When emitting `kRegister` and
   `kLatch`, the emitter prints `updateCond` and `nextValue` using
   `valueExprSeq`. If a value is **not** DPI-dependent, `valueExprSeq` returns
   the normal `valueExpr` (which may be a wire or `always_comb` temp). If it
   **is** DPI-dependent, `valueExprSeq` calls `dpiInlineExpr` to inline the
   expression tree directly, substituting DPI results with `_intm`.

Net effect: only expressions that are **used inside sequential blocks** and
**depend on DPI** are folded into the same `always` block by inlining. The
clock/event (seq key) and `updateCond` structure are not changed; only the
expression text is rewritten to avoid a delta-cycle through a wire.

## Key Changes (code locations)

- `src/convert.cpp`: classify DPI import types as `int`, `longint`, `bit`,
  `real`, `string`, etc, and only compute widths for integral types.
- `src/emit.cpp`: emit `bit` vector returns, and inline DPI-dependent
  expressions in sequential assignments.
- `tests/data/xs-bugcase/CASE_002/tb.cpp`: per-model DPI `jtag_tick` counter.
- `tests/data/xs-bugcase/CASE_002/Makefile`: `CCACHE_DIR` and
  `verilated_dpi.cpp` linkage.

## Tests

```
cmake --build build -j$(nproc)
make -C tests/data/xs-bugcase/CASE_002 run
```

Result:

```
[COVERAGE] line coverage 91.11% (82/90)
```
