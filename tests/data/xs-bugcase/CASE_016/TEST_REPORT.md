# CASE_016 Test Report

## Summary

**Status:** ✅ **FIXED** (2026-02-17)

**DUT:** `inner_ftq` (Ftq.sv)  
**Divergence Signal:** `io_fromBpu_prediction_ready`  
**Root Cause:** Wolf-sv-parser width context propagation bug in ConditionalExpression  
**Fix Location:** `src/convert.cpp`  

---

## Bug Description

### Original Issue
```
[MISMATCH] cycle=20 io_fromBpu_prediction_ready ref=0 wolf=1
```

Wolf generated code incorrectly sliced MUX branches to 1 bit before comparison:
```verilog
// Wolf (BEFORE FIX) - WRONG!
assign __expr_208 = _GEN_3[0];              // Only bit [0] - truncated too early!
assign __expr_211 = __expr_210[0];          // Only bit [0]
assign __expr_212 = _io_fromBpu_prediction_ready_T_9 ? __expr_208 : __expr_211;  // 1-bit
assign __expr_213 = {{6{1'b0}},__expr_212} < __const_39;  // Compare 1-bit value

// Wolf (AFTER FIX) - CORRECT!
assign __expr_210 = _io_fromBpu_prediction_ready_T_9 ? _GEN_3 : __expr_209;  // 7-bit MUX
assign __expr_211 = __expr_210 < __const_37;  // Compare full 7-bit value
```

---

## Root Cause Analysis

### Original Verilog (Ftq.sv line 24551-24555)
```verilog
wire io_fromBpu_prediction_ready_0 =
  ~(~_io_fromBpu_prediction_ready_T & _io_fromBpu_prediction_ready_T_5[6])
  & (_io_fromBpu_prediction_ready_T_9
       ? _GEN_3                                    // 7-bit
       : 7'(_io_fromBpu_prediction_ready_T_12 - _GEN_2))  // 7-bit
     < 7'h8 & ~(bpTrainStallCnt[3]);
```

The comparison `< 7'h8` should operate on the **full 7-bit value** from the MUX.

### Problem: Width Context Propagation
The original `convert.cpp` propagated `widthContext` into sub-expressions:

1. Assignment target is 1 bit → `widthContext = 1`
2. This context propagated into MUX branches
3. Branches were prematurely truncated to 1 bit
4. Comparison became `{6'b0, 1bit} < 8` instead of `7bit < 8`

---

## Fix Applied

### Design Principle
**"Sub-expressions should be lowered with natural width; explicit resizing only at assignment boundaries"**

### Changes in `src/convert.cpp`

#### 1. Assignment Handler (line ~2209)
```cpp
// BEFORE: Propagated width context into RHS
WidthContextScope widthScope(*this, targetWidth);

// AFTER: Reset width context, explicit resize at boundary
WidthContextScope widthScope(*this, 0);  // Preserve natural widths
// ...
if (targetWidth > 0) {
    value = resizeValueToWidth(value, targetWidth, signExtend, ...);
}
```

#### 2. ConditionalExpression Handler (line ~7557)
```cpp
// Reset width context for MUX branches
{
    WidthContextScope widthScope(*this, 0);
    lhs = lowerExpression(cond->left());
    rhs = lowerExpression(cond->right());
}

// Optimization: Direct constant truncation to reduce Verilator warnings
if (widthContext > 0 && lhsIsConst && rhsIsConst) {
    lhs = resizeValueToWidth(lhs, widthContext, signExtend, ...);
    rhs = resizeValueToWidth(rhs, widthContext, signExtend, ...);
}

// Create MUX with natural width branches
ExprNodeId muxResult = addNodeForExpr(std::move(node));

// Resize result if needed (not branches)
if (widthContext > 0) {
    return resizeValueToWidth(muxResult, widthContext, signExtend, ...);
}
```

### Changes in `src/emit.cpp`

#### 3. extendOperand Function (line ~1767)
```cpp
// BEFORE: Truncation not handled, implicit truncation caused WIDTHTRUNC warnings
if (diff <= 0)
{
    return name;  // BUG: No explicit slice for truncation
}

// AFTER: Smart truncation only when needed
if (diff == 0)
{
    return name;  // Same width, no change needed
}
if (diff < 0)  // Truncation: targetWidth < sourceWidth
{
    if (targetWidth == 1)
        return name + "[0]";  // Single bit
    else
        return name + "[" + std::to_string(targetWidth-1) + ":0]";  // Multi-bit slice
}
// diff > 0: Extension logic (unchanged)
```

### Generated IR Structure
```
Assignment: lhs (1-bit) = rhs (natural width)
                           ↓
    RHS lower with widthContext=0 (preserve widths)
                           ↓
    Sub-expressions: full widths (7-bit, 32-bit, etc.)
                           ↓
    Operations: natural width results
                           ↓
    resizeValueToWidth (explicit IR nodes):
        - Truncate: kSliceDynamic
        - Extend: kReplicate + kConcat
                           ↓
    Assignment to lhs (emit adds [0] or [N:0] slice if needed)
```

---

## Verification

### Test Results
| Test | Status |
|------|--------|
| CASE_016 | ✅ PASS |
| CASE_004 (related width bug) | ✅ PASS |
| All convert/emit tests (17) | ✅ PASS |
| C910 WIDTHTRUNC warnings | ✅ **ELIMINATED (0)** |

### Before Fix
```
$ ./sim_CASE_016
[MISMATCH] cycle=20 io_fromBpu_prediction_ready ref=0 wolf=1

# C910 build
%Warning-WIDTHTRUNC: ... expects 1 bits but generates 32 bits (4 instances)
```

### After Fix
```
$ ./sim_CASE_016
- tb.v:1721: Verilog $finish
Test passed (no mismatch)

# C910 build - No WIDTHTRUNC warnings!
# Generated code shows explicit slices:
# assign db_input_tea = __expr_275[0];
# assign trans_clk_posedge_en = __expr_89[0];
```

### Generated Code Examples
```verilog
// 1-bit target - explicit [0] slice added
assign db_input_tea = __expr_275[0];
assign trans_clk_posedge_en = __expr_89[0];
assign clk_negedge_en = __expr_8[0];

// Multi-bit target - explicit [N:0] slice
assign __expr_72 = __expr_71[31:0];
assign __expr_48 = __expr_47[3:0];

// Same width - no slice needed (smart optimization)
assign same_width = other_signal;  // Both 32-bit
```

---

## Related Bug: CASE_004

This fix extends the principle from **CASE_004** (commit `ba42623`):

| Bug | Expression Type | Root Cause | Fix |
|-----|-----------------|------------|-----|
| CASE_004 | `ConversionExpression` (e.g., `9'(...)`) | Width context propagated into cast operand | Reset context for explicit casts |
| CASE_016 | `ConditionalExpression` (e.g., `cond ? a : b`) | Width context propagated into branches | Reset context for MUX branches |

Both fixes ensure sub-expressions preserve natural widths during lowering.

---

## Waveform Comparison

### Original XS Simulation (FST)
```
Time (ps)  | io_fromBpu_prediction_ready
-----------|---------------------------
0          | 1 (initial)
1154       | 0 ⬅️ Divergence point
1218       | 1
1240       | 0
...        | ...
```

### CASE_016 Bug Case (After Fix)
```
Cycle | io_fromBpu_prediction_ready
------|---------------------------
0-19  | 1 (initial/reset)
20    | ref=0, wolf=0 ⬅️ MATCH!
```

---

## Files

| File | Description |
|------|-------------|
| `rtl/Ftq.sv` | DUT (148,441 lines) |
| `rtl/ResolveQueue.sv` | Sub-module |
| `rtl/CommitQueue.sv` | Sub-module |
| `tb.v` | Testbench with trace replay |
| `tb.cpp` | C++ test framework |
| `events.json` | Extracted waveform events |
| `signals.txt` | 427 input signal names |
| `Makefile` | Build automation |

---

## Build Instructions

```bash
cd tests/data/xs-bugcase/CASE_016

# Full build and test (~3 minutes)
make

# Quick test (if already built)
make quick

# Individual steps
make build_ref    # Build reference
make build_wolf   # Build wolf version
make run          # Run comparison
```

---

## History

| Date | Event |
|------|-------|
| 2026-02-16 | Bug case created, divergence reproduced |
| 2026-02-17 | IR analysis completed, root cause identified |
| 2026-02-17 | Fix applied: reset width context in assignment/MUX handlers (`convert.cpp`) |
| 2026-02-17 | Emit fix applied: explicit truncation slices (`emit.cpp`) |
| 2026-02-17 | C910 WIDTHTRUNC warnings eliminated (4 → 0) |

---

## Technical Notes

### Why Width Context Propagation Was Wrong

**SystemVerilog semantics:**
```verilog
assign x = (cond ? a : b) < 8;  // Compare FULL widths, then check < 8
```

**Original wolf behavior (wrong):**
```
widthContext = 1 (from assignment target)
    ↓
MUX branches truncated to 1 bit
    ↓
Compare {7'b0, 1bit} < 8  // WRONG semantics!
```

**Fixed wolf behavior (correct):**
```
widthContext = 0 (reset)
    ↓
MUX branches keep natural widths
    ↓
Compare 7bit_value < 8  // CORRECT semantics!
    ↓
Result truncated to 1 bit at assignment
```

### IR Operations for Width Adjustment

The `resizeValueToWidth` function creates explicit IR operations:

| Case | Generated IR | SV Output |
|------|-------------|-----------|
| Truncate 7→1 bit | `kSliceDynamic[value, 0, 1]` | `value[0]` |
| Truncate 32→8 bit | `kSliceDynamic[value, 0, 8]` | `value[7:0]` |
| Zero-extend 1→7 bit | `kReplicate[0, 6] + kConcat` | `{6'b0, value}` |
| Sign-extend 1→7 bit | `kReplicate[signBit, 6] + kConcat` | `{6{value[0]}, value}` |

**Smart Truncation in Emit:**
- `diff == 0`: No slice generated (e.g., `assign a = b;` when both are 32-bit)
- `diff < 0, targetWidth == 1`: `[0]` slice (e.g., `assign x = y[0];`)
- `diff < 0, targetWidth > 1`: `[N:0]` slice (e.g., `assign x = y[7:0];`)

These explicit operations eliminate Verilator WIDTHTRUNC warnings by making width conversions visible in the generated SV code.

---

*Report updated: 2026-02-17*  
*Status: RESOLVED*
