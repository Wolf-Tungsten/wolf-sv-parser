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

## Emit performance note (post-fix timeout investigation)

You reported an emit-stage timeout after the DPI inlining change. Based on the
current `src/emit.cpp` implementation, the likely hot spots are:

1) **`dpiInlineExpr` has no memoization.**  
   Every time a DPI-dependent value is used in a `kRegister`/`kLatch`, the
   emitter rebuilds the full expression tree string from scratch, walking
   the same subgraph repeatedly. In large graphs with many registers reusing
   the same combinational ValueIds, this can balloon to **O(U × E)** string
   construction work (U = number of sequential uses, E = edges in the comb DAG).

2) **Heavy string building in recursive inlining.**  
   `dpiInlineExpr` and its helpers allocate `std::ostringstream` and concat
   strings at each node. With deep expression trees, this becomes a large
   constant factor even if the dependency check is cached.

3) **Repeated `valueDependsOnDpi` checks inside `dpiInlineExpr`.**  
   The dependency DFS is cached, but it is still consulted many times during
   inline expansion. This is not the main asymptotic issue, but it adds
   overhead in tight loops when emit is already dominated by string ops.

### Possible optimizations (low-risk first)

1) **Memoize `dpiInlineExpr` results per ValueId.**  
   Add a `std::unordered_map<grh::ir::ValueId, std::string, ValueIdHash>`
   cache. Once an inline expression is built, reuse it for all later uses.
   Keep `dpiInlineResolving` to break cycles. This directly cuts the repeated
   rebuild cost in large graphs.

2) **Precompute DPI dependency flags once.**  
   Instead of recursive `valueDependsOnDpi` calls, compute a `std::vector<bool>`
   (or `unordered_map`) once per module using a topological walk of the DAG.
   This makes inlining checks O(1) without re-traversal or hashing.

3) **Use block-local temporaries for repeated DPI-dependent exprs.**  
   If multiple sequential assignments in the *same* `always` block use the
   same DPI-dependent expression, emit one blocking temporary at the top of
   the block, then reuse it:
   ```
   always @(posedge clk) begin
     if (__expr_5) _dpi_ret_4_intm = jtag_tick(...);
     tmp_dpi_expr = (__expr_5 ? _dpi_ret_4_intm : 32'd0);
     if (condA) regA <= tmp_dpi_expr;
     if (condB) regB <= tmp_dpi_expr;
   end
   ```
   This stays within the same `always` (no delta), but avoids duplicate
   inline string generation.

4) **Optional: cache `valueExpr` for non-DPI values used in `valueExprSeq`.**  
   For large modules, even non-DPI `valueExpr` calls can be substantial. A
   general expression cache (keyed by ValueId) can reduce total emit time.

**Implemented in this fix:** (1) + (2) are now applied in `src/emit.cpp`.
`dpiInlineExpr` results are cached, and DPI dependency flags are precomputed
once per module. In addition, the dependency map now uses dense per-ValueId
arrays (indexed by `ValueId.index`) to avoid unordered_map hash overhead on
large graphs.

**Additional optimization (post-timeout):** value name lookups and
`materialize/resolving` membership checks now use dense, per-ValueId arrays
instead of `unordered_set`/repeated `getValue()` string extraction. This avoids
millions of hash lookups and symbol-text allocations during emit.

**Streaming output (option 1):** emit now writes each module directly to the
output stream instead of building a huge in-memory `moduleBuffer` and dumping
at the end. This avoids large string concatenations and lowers peak memory
usage during emit.

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
