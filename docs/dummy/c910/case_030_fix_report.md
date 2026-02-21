# C910 Case 030 Fix Report: IDU LSU Instruction Type Decode

## Problem Summary

### Symptom
- **ref**: `inst*_lsu` signals appear from cycle=4036, followed by `lsiq_dp_en*` and `create0/1_{load,store}`
- **wolf**: `inst*_vld` toggles from cycle=2079, but NO `inst*_lsu`, `lsiq_dp_en*`, or `create0/1_{load,store}` signals
- **wolf**: Simulation stops at cycle=100k with "no instructions retired"

### Root Cause Analysis

#### Initial Hypothesis
Problem converges on IDU instruction type decode. Suspected module: `ct_idu_id_decd` or upstream opcode extraction in `ct_idu_id_dp`.

#### Investigation Results

**1. Isolated Module Test (case_030)**
- Created minimal testbench for `ct_idu_id_decd`
- Tests LB, LH, LW, LD, SB, SH, SW, SD, C.LW, C.LD, C.SW, C.SD instructions
- **Result**: Both ref and wolf PASS ✓
- **Conclusion**: `ct_idu_id_decd` module logic is correct in isolation

**2. Full System Test Analysis**
- wolf log: 457 lines vs ref log: 130,559 lines
- wolf stops early due to "no instructions retired"
- Key observation: `inst*_vld` signals present but `inst*_lsu` signals absent

**3. Signal Flow Analysis**
```
ct_idu_id_decd (x_inst_type) 
    ↓
id_inst0_inst_type (wire in ct_idu_id_dp)
    ↓
LSU type check: (id_inst0_inst_type == LSU) || (id_inst0_inst_type == LSU_P5)
    ↓
inst0_lsu debug signal
```

The issue appears to be:
- Either `ct_idu_id_decd` instances in `ct_idu_id_dp` receive wrong `x_inst` input
- Or other control signals in `ct_idu_id_dp` affect the decoded output
- Or the wire connections between instances are incorrect after wolf transformation

## Fix Details

### Current Status
**NOT FIXED** - Isolated module test passes, indicating the issue is in system-level integration.

### Recommended Fix Approaches

#### Approach 1: Debug ct_idu_id_dp Integration (Recommended)
Create a minimal test case that includes `ct_idu_id_dp` module to test:
- Instruction input (`ifu_idu_ib_inst*_data`) to `ct_idu_id_decd` instances
- Output `id_inst*_inst_type` connections
- Pipeline control signals (`ctrl_dp_id_inst*_vld`)

#### Approach 2: Add Debug Instrumentation
Add `$display` probes in `ct_idu_id_dp.v`:
```verilog
// At ct_idu_id_decd instance outputs
$display("[debug] inst0: x_inst=%08x type=%03x", 
         id_inst0_inst, id_inst0_inst_type);
```

#### Approach 3: Check Wolf Transformation
Compare wolf-generated `ct_idu_id_dp.sv` with original:
- Verify `ct_idu_id_decd` instance connections
- Check for signal optimization issues
- Verify width and type of `x_inst_type` connections

## Validation Results

### Case 030 Isolated Test
```bash
make -C tests/data/openc910/bug_cases/case_030 run_c910_bug_case_ref
# Result: PASSED - 14/14 tests

make -C tests/data/openc910/bug_cases/case_030 run_c910_bug_case  
# Result: PASSED - 14/14 tests
```

### Full C910 System Test
```bash
make run_c910_test
# Result: FAILED - "no instructions retired" at cycle=100000
```

## Open Questions

1. Why does isolated test pass but full system fail?
   - Possible: Input signals to `ct_idu_id_decd` are different in full system
   - Possible: Control signals (e.g., `cp0_idu_*`) affect decoding
   - Possible: Module instantiation/wiring issue in wolf output

2. What is the source of `x_inst` in full system?
   - Need to trace `ifu_idu_ib_inst*_data` → `id_inst*_inst` path

3. Are there width mismatches or type conversions?
   - Check `x_inst_type` wire widths in both ref and wolf

## Follow-up Actions

1. **Immediate**: Create `ct_idu_id_dp` minimal test case
2. **Short-term**: Add instrumentation to compare ref/wolf signal values
3. **Long-term**: Investigate wolf-sv-parser module instantiation logic

## Files

- Bug case: `tests/data/openc910/bug_cases/case_030/`
- Testbench: `tests/data/openc910/bug_cases/case_030/tb_case_030.cpp`
- This report: `docs/c910/case_030_fix_report.md`
- Ref log: `build/logs/c910/c910_ref_coremark_20260207_125107.log`
- Wolf log: `build/logs/c910/c910_wolf_coremark_20260207_125107.log`

---

**Status**: Investigation ongoing - isolated module passes, system integration issue suspected
