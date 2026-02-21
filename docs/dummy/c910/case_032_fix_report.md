# case_032 Fix Report: fpga_ram read latency

## Problem summary
- Failure signature: `tb_case_032.cpp` read check failed (`read addr0 mismatch: got=0x0 expected=0x1`) when running wolf emit.
- Origin: `docs/c910/c910_diff_report.md` (2026-02-08 15:54) flagged an extra read cycle in wolf-emitted `fpga_ram` that delayed I-cache tag RAM reads.

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_032`
- DUT: `fpga_ram`
- RTL source: `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/fpga/rtl/fpga_ram.v`
- Coverage (ref): 100% line (derived from DA entries)

## Root cause
`emitMemoryRead` in `src/convert.cpp` modeled synchronous memory reads by inserting a register (`mem_read_reg`) on top of a memory read port. When the read value was then assigned to a sequential output register, the design effectively gained an extra register stage. For `fpga_ram`, this produced a one-cycle read latency (`__mem_data_0` then `PortADataOut`), which diverged from the RTL behavior.

## Fix
- Remove the extra register stage for synchronous memory reads by emitting the memory read port result directly. This keeps the read value combinational and lets the existing sequential assignments capture it on the correct edge.
- File changed: `src/convert.cpp` (emitMemoryRead)

## Validation
- Repro case (ref): `make -C tests/data/openc910/bug_cases/case_032 run_c910_bug_case_ref`
  - Result: pass, coverage 100.00% (14/14)
- Repro case (wolf): `make -C tests/data/openc910/bug_cases/case_032 run_c910_bug_case`
  - Result: pass, coverage 100.00% (12/12)
- Unit tests: `ctest --test-dir build --output-on-failure`
  - Result: 23/23 passed

## Risks / follow-ups
- This changes sync-memory read modeling globally; verify other designs that rely on a deliberate extra register stage (if any) still behave as expected.
