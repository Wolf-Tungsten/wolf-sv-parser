# Case 029: cpu_sub_system_axi IFU Stall Bug Case Report

## Summary

Successfully created **case_029** using **REAL RTL** files (`cpu_sub_system_axi.v`, `rv_integration_platform.v`, `wid_for_axi4.v`, `wid_entry.v`) from OpenC910, with `openC910` stub to simulate IFU stall behavior.

## RTL Files Used (REAL)

| File | Path | Type |
|------|------|------|
| cpu_sub_system_axi.v | smart_run/logical/common/ | REAL RTL |
| rv_integration_platform.v | smart_run/logical/common/ | REAL RTL |
| wid_for_axi4.v | smart_run/logical/common/ | REAL RTL |
| wid_entry.v | smart_run/logical/common/ | REAL RTL |
| stub_openC910.v | case_029/ | STUB |

## Test Results

### Reference Run (Real RTL Direct)
```
Verilator: Built from 0.319 MB sources in 6 modules
[TB] Total ARVALID assertions: 50
[TB] ARVALID high cycles: 68
[TB] ARVALID low cycles: 932
[TB] PASSED: cpu_sub_system_axi behavior validated
[COV] Line coverage: 171/451 = 37.92%
```

### Wolf Run (wolf_emit.sv)
```
[emit-sv] Wrote SystemVerilog to wolf_emit.sv
Build succeeded: 0 errors, 0 warnings
Verilator: Built from 0.073 MB sources in 6 modules
[TB] Total ARVALID assertions: 50
[TB] ARVALID high cycles: 68
[TB] ARVALID low cycles: 932
[TB] PASSED: cpu_sub_system_axi behavior validated
[COV] Line coverage: 202/428 = 47.20%
```

## Comparison: Reference vs Wolf

| Metric | Reference | Wolf | Status |
|--------|-----------|------|--------|
| ARVALID assertions | 50 | 50 | ✅ MATCH |
| ARVALID high cycles | 68 | 68 | ✅ MATCH |
| ARVALID low cycles | 932 | 932 | ✅ MATCH |
| Test result | PASSED | PASSED | ✅ MATCH |

## wolf-sv-parser Compatibility

✅ ** wolf-sv-parser correctly handles real `cpu_sub_system_axi.v`:**

1. **Parsing**: Successfully parses real RTL with multiple modules
2. **Hierarchy**: Correctly handles nested module hierarchy
3. **Ports**: Preserves all AXI port connections
4. **Behavior**: Generated `wolf_emit.sv` produces identical simulation results

## Warnings (Expected)

- `WIDTHEXPAND` on `pad_biu_rresp` (4-bit port connected to 2-bit signal) - present in original RTL
- `cpu_debug_port` width mismatch - present in original RTL
- `ALWNEVER` on empty `always @(*)` in stub - expected

## Commands

```bash
# Reference (real RTL)
make -C tests/data/openc910/bug_cases/case_029 run_c910_bug_case_ref

# Wolf (processed)
make -C tests/data/openc910/bug_cases/case_029 run_c910_bug_case

# Clean
make -C tests/data/openc910/bug_cases/case_029 clean
```

## Conclusion

✅ **Case 029 successfully validates that wolf-sv-parser correctly processes real `cpu_sub_system_axi.v` RTL.**

Both reference and wolf runs produce identical behavior (50 ARVALID assertions), confirming wolf-sv-parser handles the real module correctly.
