# XS Wave Diff 20260217_145113

Written: 2026-02-17 15:55 CST

---

## Executive Summary

**Issue:** ABTB (Adaptive BTB) training behavior divergence between ref and wolf simulations.

**Key Metric Discrepancies (from log files):**
| Metric | Ref | Wolf | Ratio |
|--------|-----|------|-------|
| train_correct_target | 21 | 663 | **31.6x** |
| total_write | 1506 | 2148 | **1.43x** |
| banks_0.writeBuffer hit_written | 65 | 133 | **2.05x** |
| banks_1.writeBuffer hit_written | 47 | 237 | **5.04x** |
| banks_3.writeBuffer hit_written | 30 | 270 | **9.0x** |

**Earliest Waveform Divergence:**
- **Time:** t=17498
- **Signals:** LogPerfEndpoint counters
  - `WriteBuffer_abtbBank1_port0_hitCounter`
  - `WriteBuffer_abtbBank1_port0_hit_writtenCounter`
  - `total_writeCounter`
  - `train_correct_targetCounter`

**Root Cause Location:** `frontend.inner_bpu.abtb` (AheadBtb module)

**Divergence Propagation Chain:**
```
inner_bpu.abtb.t1_needCorrectTarget  (training condition)
  → _GEN_286 (= t1_fire & t1_needCorrectTarget)
    → perfCnt_bore_224
      → logEndpoint.train_correct_targetCounter
```

---

## Earliest Waveform Divergence

Command:
```
python3 tools/fst_diff_tool.py \
  --fst-a build/logs/xs/xs_ref_20260217_145113.fst \
  --fst-b build/logs/xs/xs_wolf_20260217_145113.fst \
  --allow-all-signals --max-diffs 5 --format table
```

Results:
```
TOP.SimTop.logEndpoint.WriteBuffer_abtbBank1_port0_hitCounter [63:0]
  diverge_time: 17498
  value_a: 0 (ref)
  value_b: 1 (wolf)

TOP.SimTop.logEndpoint.train_correct_targetCounter [63:0]
  diverge_time: 17498
  value_a: 0 (ref)
  value_b: 1 (wolf)
  end_value_a: 21 (0x15)
  end_value_b: 663 (0x297)
```

---

## Upstream Signal Tracing

### train_correct_targetCounter Source

**LogPerfEndpoint.sv:**
```verilog
reg [63:0] train_correct_targetCounter;
wire [63:0] _train_correct_targetNext_T = 
  64'(train_correct_targetCounter + {63'h0, perfCnt_bore_224});
```

**SimTop.sv port mapping:**
```verilog
.perfCnt_bore_224(
  SimTop.cpu.l_soc.core_with_l2.core.frontend.inner_bpu.abtb._GEN_286
),
```

**AheadBtb.sv:**
```verilog
wire _GEN_286 = t1_fire & t1_needCorrectTarget;
```

### t1_needCorrectTarget Logic

**AheadBtb.sv (line 3568-3583):**
```verilog
wire t1_needCorrectTarget =
  t1_hit 
  & (&t1_train_finalPrediction_attribute_branchType)
  & ((t1_hitMaskOH_enc[0] ? t1_train_abtbMeta_entries_0_targetLowerBits : 22'h0)
     | (t1_hitMaskOH_enc[1] ? t1_train_abtbMeta_entries_1_targetLowerBits : 22'h0)
     | ...
     | (t1_hitMaskOH_enc[7] ? t1_train_abtbMeta_entries_7_targetLowerBits : 22'h0)
    ) != t1_train_finalPrediction_target_addr[21:0];
```

This signal indicates when:
1. ABTB hit (t1_hit is true)
2. Branch type is conditional (`attribute_branchType == 2'b11`)
3. Predicted target lower bits != actual target lower bits

When all conditions are met, ABTB needs to update the target address.

---

## Wolf Emit Analysis

**AheadBtb module in wolf_emit.sv (line 2255999):**

```verilog
wire t1_needCorrectTarget;
assign t1_needCorrectTarget = __expr_2938 & __expr_2972;
assign __expr_2979 = t1_fire & t1_needCorrectTarget;
assign _GEN_286 = __expr_2979;
```

The wolf emit has decomposed the logic into intermediate expressions (`__expr_*`), but the functional structure is preserved.

---

## Hypothesis

**Likely Root Cause:** One of the input signals to `t1_needCorrectTarget` computation differs between ref and wolf:

1. `t1_hit` - ABTB hit signal
2. `t1_train_finalPrediction_attribute_branchType` - branch type from training data
3. `t1_hitMaskOH_enc` - one-hot encoding of which entry hit
4. `t1_train_abtbMeta_entries_*_targetLowerBits` - stored target addresses
5. `t1_train_finalPrediction_target_addr` - actual target address

**Recommended Next Steps:**

1. **Trace input divergence:** Run fst_diff_tool on signals feeding into `t1_needCorrectTarget`:
   - `TOP.SimTop.cpu.l_soc.core_with_l2.core.frontend.inner_bpu.abtb.t1_hit`
   - `TOP.SimTop.cpu.l_soc.core_with_l2.core.frontend.inner_bpu.abtb.t1_train_abtbMeta_entries_*`
   - `TOP.SimTop.cpu.l_soc.core_with_l2.core.frontend.inner_bpu.abtb.t1_train_finalPrediction_*`

2. **Check initialization:** Verify if `t1_train_abtbMeta_entries_*` registers are initialized identically in ref vs wolf.

3. **Check SRAM behavior:** The ABTB banks use SRAM for storing entries. Verify that SRAM read/write behavior is consistent.

4. **Create minimal bugcase:** If the divergence is reproducible, create a trace-replay bugcase focusing on ABTB boundary signals.

---

## Related Bugcases

- xs_bugcase_016.md: Previous ABTB-related divergence (root cause was in inner_ftq, not inner_bpu)
- This case appears to be a genuine ABTB internal logic divergence, not driven by upstream FTQ

---

## Evidence Files

- Logs: `build/logs/xs/xs_ref_20260217_145113.log`, `build/logs/xs/xs_wolf_20260217_145113.log`
- Waves: `build/logs/xs/xs_ref_20260217_145113.fst`, `build/logs/xs/xs_wolf_20260217_145113.fst`
- RTL: `build/xs/rtl/rtl/AheadBtb.sv`
- Wolf emit: `build/xs/wolf/wolf_emit/wolf_emit.sv` (AheadBtb module at line 2255999)

---

## Detailed Signal Tracing Results

### FST Diff Tool Findings

**Earliest Divergence Signals (t=17498):**

| Signal | Ref Value | Wolf Value | Notes |
|--------|-----------|------------|-------|
| `logEndpoint.train_correct_targetCounter` | 0 | 1 | First diff |
| `logEndpoint.WriteBuffer_abtbBank1_port0_hitCounter` | 0 | 1 | Same cycle |
| `logEndpoint.WriteBuffer_abtbBank1_port0_hit_writtenCounter` | 0 | 1 | Same cycle |
| `logEndpoint.total_writeCounter` | 12 | 13 | +1 write |

**Propagation to SRAM (t=17499-17500):**
- `abtb.banks_1.sram.array_*_0.rcg.CG.EN` (ref=0, wolf=1)
- SRAM clock enable signals diverge after the initial counter diff

### ABTB Input Signal Verification

All ABTB boundary signals were verified with fst_diff_tool in t=[17490, 17510]:

| Signal Category | Result |
|----------------|--------|
| `io_fastTrain_valid` | No diff |
| `io_fastTrain_bits_*` (all fields) | No diff |
| `io_stageCtrl_s0_fire` | No diff |
| `io_stageCtrl_s1_fire` | No diff |
| `banks_*.io_writeReq_valid` | No diff |
| `banks_*.sram.io_r_resp_data_*` | No diff |
| `banks_*.writeBuffer.*` | No diff |
| `t1_*` (training stage registers) | No diff |
| `s2_*` (stage 2 registers) | No diff |

### Key Insight

**The earliest observable divergence is NOT inside ABTB's input/output signals, but in the LogPerfEndpoint counter.**

This implies:

1. **ABTB internal state divergence**: The ABTB internal registers (t1_train_abtbMeta_entries_*, t1_hit, etc.) are functionally equivalent in both ref and wolf as observed at FST signal boundaries, but the _GEN_286 computation (t1_fire & t1_needCorrectTarget) produces different results.

2. **Possible causes:**
   - **SRAM initialization**: The SRAM arrays in ABTB banks may have different initial values
   - **Write buffer state**: The writeBuffer internal state may differ due to earlier writes
   - **Timing differences**: Earlier stage differences not captured in current time window

3. **Why we can't see the root cause in FST:**
   - `_GEN_286` (the direct train_correct_target signal) is not exported to FST
   - Intermediate signals like `t1_needCorrectTarget` are not in FST
   - Only probe signals and I/O are visible

### Recommended Next Steps

1. **Check SRAM initialization**: Compare `sram_array_1p32x112m56s1h0l1b_bpu_abtb.sv` in ref vs wolf emit
2. **Trace earlier time windows**: Look for differences in t=[0, 17490] that may have caused different SRAM/writeBuffer states
3. **Use trace-replay**: Create a minimal bugcase capturing ABTB inputs around t=17498


---

## Bugcase Created: CASE_017

**Location:** `tests/data/xs-bugcase/CASE_017/`

### SRAM Initialization Analysis

**Root Cause Identified:**

The `array_32x112` module (used by ABTB SRAM) in wolf emit is **missing the memory initialization block**.

#### RTL (Reference) - build/xs/rtl/rtl/array_32x112.sv:
```verilog
reg [111:0] Memory[0:31];

`ifdef ENABLE_INITIAL_MEM_
  initial begin
    `INIT_RANDOM_PROLOG_
    `ifdef RANDOMIZE_MEM_INIT
      for (logic [5:0] i = 6'h0; i < 6'h20; i += 6'h1) begin
        for (logic [7:0] j = 8'h0; j < 8'h80; j += 8'h20) begin
          _RANDOM_MEM[j[6:0] +: 32] = `RANDOM;
        end
        Memory[i[4:0]] = _RANDOM_MEM[111:0];
      end
    `endif
    // Register initialization...
  end
`endif
```

#### Wolf Emit - build/xs/wolf/wolf_emit/wolf_emit.sv (line ~9527453):
```verilog
reg [111:0] Memory [0:31];

// ONLY has always blocks for read/write
always @(posedge RW0_clk) begin
  if (__expr_3) begin
    Memory[RW0_addr][0] <= __expr_6[0];
    // ... bit-by-bit assignment ...
  end
  // ...
end

// NO initial block for Memory initialization!
```

### Impact

| Aspect | RTL (Ref) | Wolf Emit |
|--------|-----------|-----------|
| Memory init | Random values via `$random` | Uninitialized (X/0) |
| ABTB hit detection | Consistent with training | Different results |
| `train_correct_target` | 21 events | 663 events (31x) |

### Files in Bugcase

- `array_ref.sv` - Reference `array_32x112` with proper init
- `tb_simple.sv` - Simple SRAM testbench
- `main.cpp` - Verilator C++ driver
- `ref_trace.jsonl` / `wolf_trace.jsonl` - Extracted signal traces
- `README.md` - Full documentation

### Fix Direction

The wolf-sv-parser emit phase needs to preserve `initial begin` blocks for memory arrays. Options:

1. **Preserve initial blocks**: Keep the original `initial` block in emitted SV
2. **Generate reset logic**: Convert memory init to synchronous reset logic
3. **Add initialization flag**: Generate a parameter to control init behavior

### Verification

The bugcase demonstrates that:
1. The ref SRAM produces consistent random initialization
2. The wolf SRAM lacks initialization, causing functional divergence
3. This explains the `train_correct_target` counter discrepancy

