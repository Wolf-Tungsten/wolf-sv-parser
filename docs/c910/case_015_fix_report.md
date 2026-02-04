# case_015 Fix Report

## Problem summary
- Verilator failed to compile wolf-emitted SV for `mem_ctrl` because `hclk` and `hrst_b` were referenced in always sensitivity lists but never declared or assigned.
- Original failure captured in `build/artifacts/c910_run.log` with errors in `tests/data/openc910/smart_run/work/wolf_emit/sim_top_wolf.sv`.

## Failure signature
```
%Error: .../sim_top_wolf.sv:35835:20: Can't find definition of variable: 'hclk'
%Error: .../sim_top_wolf.sv:35835:36: Can't find definition of variable: 'hrst_b'
```

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_015`
- DUT top: `mem_ctrl`
- RTL sources: `tests/data/openc910/smart_run/logical/mem/mem_ctrl.v`, `tests/data/openc910/smart_run/logical/mem/ram.v`
- Ref coverage: 92.93% (171/184)

## Root cause analysis
- The SV emitter uses `eventEdge` metadata to build sensitivity lists but prints the raw symbol name (`symbolText`) instead of resolving trivial assigns.
- Alias nets like `hclk`/`hrst_b` are defined by `assign hclk = pll_core_cpuclk;` / `assign hrst_b = pad_cpu_rst_b;` and are otherwise unused except in sensitivity lists.
- The emitter skipped materializing assign results unless explicitly required, so these alias nets were never declared/emitted, leaving unresolved symbols in the output.

## Fix details
- Marked all event-edge operands as “materialized” during emission so their wires and assigns are emitted.
- Implemented in both graph-based and view-based emission paths.
- Files changed: `src/emit.cpp`.

## Validation
- `make -C tests/data/openc910/bug_cases/case_015 run_c910_bug_case_ref`
  - Coverage: 92.93% line coverage (171/184)
- `make -C tests/data/openc910/bug_cases/case_015 run_c910_bug_case`
  - Coverage: 94.64% line coverage (159/168)
  - Verilator reports a non-fatal `UNOPTFLAT` warning in wolf output.
- `ctest --test-dir build --output-on-failure`

## Follow-ups / risks
- No functional change to logic emission beyond ensuring clock/reset aliases used in sensitivity lists are declared/assigned.
- If additional event-edge operand conventions are introduced, they should continue to be added to materialization.
