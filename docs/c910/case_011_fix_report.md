# case_011 fix report - instance output to unpacked net array

## Problem summary
- Original failure: wolf-sv-parser aborted conversion with
  `Skipping instance with missing output binding`.
- Signature (from `build/artifacts/c910_run.log`):

```
[convert] [ERROR] ../../C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_kid_busif.v:531:19 - Skipping instance with missing output binding
  statement: output [DATA-1:0] data_out;
```

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_011`
- DUT top: `c910_inst_reg_mem_top`
- Key RTL: `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_kid_busif.v`
- Coverage (ref): 100% line coverage (18/18)

## Root cause
- Unpacked net arrays (e.g., `wire [31:0] mem [1:0]`) were treated as memories because
  `memoryRows > 0` triggers memory classification.
- Memory-backed signals do not get a value created during graph assembly; instance
  outputs connected to such targets therefore failed `valueForSymbol()` and emitted
  the missing output binding error.
- The output binding resolver and expression/LHS lowering did not account for unpacked
  net array indexing, so even if a value existed, index offsets were not flattened.

## Fix
- Treat unpacked *net* arrays as flattened vectors (not memories):
  - Create values for net arrays with width = elementWidth * rows.
  - Exclude net arrays from memory port lowering and memory op creation.
  - Use flattened widths in write-back width resolution and inout signal creation.
- Add flattening support for unpacked net array indexing:
  - Expression lowering now converts unpacked net array element selects into
    dynamic slices with correct bit offsets.
  - LHS slicing for net arrays collapses unpacked indices into an indexed part-select.
  - Instance output binding resolution supports unpacked net array element/range
    connections with constant indices.

### Files changed
- `src/convert.cpp`
- `tests/data/openc910/bug_cases/case_011/coverage_check.py`

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_011 run_c910_bug_case_ref`
  - `[COVERAGE] line coverage 100.00% (18/18)`
- `make -C tests/data/openc910/bug_cases/case_011 run_c910_bug_case`
  - Conversion succeeds; sim passes; coverage 100.00% (17/17)
  - Verilator reports `UNOPTFLAT` on `wolf_emit.sv` (non-fatal)
- `ctest --test-dir build --output-on-failure`
  - 23/23 tests passed

## Open questions / follow-ups
- The generated `wolf_emit.sv` for the repro triggers a Verilator `UNOPTFLAT`
  warning about a combinational loop. Worth confirming this is benign for the
  full C910 PLIC flows and not a new structural artifact of flattening.
