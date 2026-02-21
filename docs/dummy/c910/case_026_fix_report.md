# Case 026 Fix Report - UNOPTFLAT Warning in ct_ifu_icache_if

## Problem Summary

Verilator reported `UNOPTFLAT` (unoptimizable circular combinational logic) warning when simulating wolf-sv-parser generated output for the `ct_ifu_icache_if` module:

```
%Warning-UNOPTFLAT: wolf_emit.sv:1164:14: Signal unoptimizable: Circular combinational logic: 'ct_ifu_icache_if.__expr_68'
```

The circular path involved `ifu_icache_tag_wen` and multiple `__expr_*` intermediate signals.

## Root Cause Analysis

### Original RTL Design
The original `ct_ifu_icache_if.v` has two combinational `always` blocks that write to different bits of `ifu_icache_tag_wen`:

```verilog
// Block 1: writes ifu_icache_tag_wen[2]
always @( ifctrl_icache_if_tag_wen[2] or ... )
begin
  if(ifctrl_icache_if_inv_on)
    ifu_icache_tag_wen[2] = ifctrl_icache_if_tag_wen[2];
  else if(l1_refill_icache_if_wr && l1_refill_icache_if_last)
    ifu_icache_tag_wen[2] = 1'b0;
  else
    ifu_icache_tag_wen[2] = 1'b1;
end

// Block 2: writes ifu_icache_tag_wen[1:0]
always @( ifctrl_icache_if_inv_on or ... )
begin
  if(ifctrl_icache_if_inv_on)
    ifu_icache_tag_wen[1:0] = ifctrl_icache_if_tag_wen[1:0];
  else if(...)
    ifu_icache_tag_wen[1:0] = {!fifo_bit, fifo_bit};
  else
    ifu_icache_tag_wen[1:0] = 2'b11;
end
```

**Key point**: Each block fully assigns its respective bits with definite values (inputs/constants), NEVER using the register's own current value.

### Bug in wolf-sv-parser
When wolf-sv-parser merged these two combinational blocks, it:

1. Detected the writes as partial slices (bit 2 and bits 1:0)
2. Used `applySlices(nextValue, ...)` where `nextValue` was initialized to the symbol `ifu_icache_tag_wen`
3. This created a feedback loop in the generated expression chain:
   - `ifu_icache_tag_wen` → `__expr_50` → ... → `__expr_68` → `always_latch` update

The code in `src/convert.cpp` only set `zeroBaseForSlices = true` (to avoid feedback) when `allUnconditional && slicesCoverFullWidth`. For combinational blocks with conditional writes (if-else), `allUnconditional` was false, causing the feedback path.

## Fix Details

**File**: `src/convert.cpp`

**Change**: Extended the condition for `zeroBaseForSlices` to also consider combinational domains with full slice coverage:

```cpp
const bool slicesCoverFull =
    slicesCoverFullWidth(group.writes, baseWidth, baseType, entry.target);

// For combinational domain, if slices cover the full width,
// use zero base to avoid feedback loop even if writes are conditional.
// This is safe because combinational always blocks should fully
// assign the target without using its previous value.
const bool fullCoverage =
    allUnconditional && slicesCoverFull;
const bool combinationalFullCoverage =
    (entry.domain == ControlDomain::Combinational) && slicesCoverFull;
zeroBaseForSlices = fullCoverage || combinationalFullCoverage;
```

**Rationale**: 
- Combinational always blocks (with `always @(*)`) should always provide a complete assignment
- Even if individual writes are conditional (if-else), the complete set of writes covers all bits
- Using zero as base (instead of the register's own value) prevents the circular dependency

## Validation Results

### Before Fix
```
%Warning-UNOPTFLAT: wolf_emit.sv:1164:14: Signal unoptimizable: Circular combinational logic
```

### After Fix
- No UNOPTFLAT warning
- Simulation passes successfully
- Coverage: 95.40% (332/348 lines)

### Test Results
```
Ref test:  94.57% coverage (348/368 lines) - PASSED
Wolf test: 95.40% coverage (332/348 lines) - PASSED
ctest:     23/23 tests passed - PASSED
```

## Files Changed

- `src/convert.cpp`: Modified slice handling logic for combinational domains

## Open Questions / Follow-ups

1. The fix specifically targets combinational domains. Need to verify if similar issues exist for other domain types.
2. Consider adding a regression test for this pattern in the convert test suite.

