# Case 021 Fix Report - Unsigned Comparison Warnings in axi_interconnect128

## Problem Summary

The wolf-sv-parser was emitting Verilog code that triggered Verilator warnings about unsigned comparisons:

```
%Warning-UNSIGNED: .../wolf_emit.sv:24019:28: Comparison is constant due to unsigned arithmetic
24019 |   assign __expr_0 = awaddr >= 40'h0;
      |                            ^~

%Warning-CMPCONST: .../wolf_emit.sv:24039:29: Comparison is constant due to limited range
24039 |   assign __expr_10 = awaddr <= 40'hffffffffff;
      |                             ^~
```

These warnings occurred because:
1. For unsigned values, `value >= 0` is always true
2. For an N-bit unsigned value, `value <= (2^N - 1)` is always true

The original RTL contained address range checks like:
```verilog
assign awsel0 = (awaddr>=SRAM_START) && (awaddr<=SRAM_END);
```

Where `SRAM_START = 40'h0` and `ERR2_END = 40'hff_ffffffff` (maximum value for 40-bit unsigned).

## Minimal Repro

- **Case Path**: `tests/data/openc910/bug_cases/case_021/`
- **DUT Top**: `axi_interconnect128`
- **Coverage**: 94.08% (RTL), 94.70% (wolf)

### Repro Commands

```bash
# Generate wolf output and see warnings
make -C tests/data/openc910/bug_cases/case_021 run

# Run RTL test (reference)
make -C tests/data/openc910/bug_cases/case_021 run_c910_bug_case_ref

# Run wolf test (should not show UNSIGNED/CMPCONST warnings)
make -C tests/data/openc910/bug_cases/case_021 run_c910_bug_case
```

## Root Cause Analysis

The wolf-sv-parser's constant folding pass was not handling the simplification of unsigned comparisons with boundary values:

1. `x >= 0` for unsigned `x` should always simplify to `1'b1`
2. `x <= MAX_VALUE` for unsigned `x` with width N (where MAX_VALUE = 2^N - 1) should always simplify to `1'b1`

These comparisons were being emitted as-is, causing Verilator to warn about constant comparisons.

## Fix Details

### Files Changed

1. **include/pass/const_fold.hpp**
   - Added `simplifyUnsignedComparisons()` method declaration

2. **src/pass/const_fold.cpp**
   - Implemented `simplifyUnsignedComparisons()` function that:
     - Detects `kGe` (>=) operations with unsigned LHS and constant 0 RHS
     - Detects `kLe` (<=) operations with unsigned LHS and constant all-ones RHS
     - Replaces these operations with constant `1'b1`
   - Added call to `simplifyUnsignedComparisons()` in `processSingleGraph()`

### Implementation

```cpp
bool ConstantFoldPass::simplifyUnsignedComparisons(GraphFoldContext &ctx)
{
    // For each comparison operation:
    // 1. If unsigned >= 0, replace with 1'b1
    // 2. If unsigned <= max_value (all 1s for its width), replace with 1'b1
}
```

The implementation uses the existing constant pool infrastructure to:
- Check if RHS is a known constant
- Verify LHS is unsigned
- Create a new constant `1'b1` value
- Replace all uses of the comparison result
- Erase the original comparison operation

### Example Transformation

Before:
```verilog
assign __expr_0 = awaddr >= 40'h0;     // UNSIGNED warning
assign __expr_1 = awaddr <= 40'h01ff_ffff;
assign awsel0 = __expr_0 && __expr_1;
```

After:
```verilog
assign awsel0 = 1'b1 && __expr_1;      // No warning, equivalent behavior
```

## Validation Results

### Test Results

```bash
# All existing tests pass
ctest --test-dir build --output-on-failure
# 100% tests passed, 0 tests failed out of 23

# Bug case tests pass
make -C tests/data/openc910/bug_cases/case_021 run_c910_bug_case_ref
# [COVERAGE] line coverage 94.08% (334/355)

make -C tests/data/openc910/bug_cases/case_021 run_c910_bug_case
# [COVERAGE] line coverage 94.70% (250/264)
# No UNSIGNED or CMPCONST warnings
```

### Verilator Warning Comparison

**Before fix:**
```
%Warning-UNSIGNED: .../wolf_emit.sv:521:28: Comparison is constant due to unsigned arithmetic
%Warning-CMPCONST: .../wolf_emit.sv:541:29: Comparison is constant due to limited range
%Warning-UNSIGNED: .../wolf_emit.sv:807:36: Comparison is constant due to unsigned arithmetic
%Warning-CMPCONST: .../wolf_emit.sv:827:36: Comparison is constant due to limited range
%Warning-UNSIGNED: .../wolf_emit.sv:985:30: Comparison is constant due to unsigned arithmetic
%Warning-CMPCONST: .../wolf_emit.sv:1005:30: Comparison is constant due to limited range
```

**After fix:**
```
# No UNSIGNED or CMPCONST warnings
# Only UNOPTFLAT warnings remain (unrelated to this issue)
```

## Risks and Limitations

1. **Scope**: The fix only handles `>= 0` and `<= MAX` for unsigned values. Other tautological comparisons (e.g., signed values, different boundary patterns) are not addressed.

2. **Signedness Detection**: The fix relies on the `isSigned()` property of values. If this property is incorrectly set during parsing, the simplification might not apply.

3. **Max Value Detection**: The fix checks if all bits up to the LHS width are set to 1. This correctly handles cases like `40'hffffffffff` for 40-bit values.

## Open Questions / Follow-ups

1. Should we extend this to handle signed value tautologies (e.g., `signed_val <= MAX_SIGNED`)?

2. Should we add similar simplifications for:
   - `x > MAX_VALUE` → `1'b0`
   - `x < 0` (unsigned) → `1'b0`

3. Consider moving this to a dedicated `simplify-expr` pass if more expression simplifications are added in the future.

## Commands Executed

```bash
# Build
cmake -S . -B build
cmake --build build -j$(nproc)

# Test bug case
make -C tests/data/openc910/bug_cases/case_021 run
make -C tests/data/openc910/bug_cases/case_021 run_c910_bug_case_ref
make -C tests/data/openc910/bug_cases/case_021 run_c910_bug_case

# Run all tests
ctest --test-dir build --output-on-failure
```
