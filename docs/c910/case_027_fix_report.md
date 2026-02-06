# case_027 Fix Report

## Summary
- **Issue:** wolf-sv-parser emit caused axi_slave128 memory reads to return the
  previous beat's data, diverging from ref at cycle ~2132.
- **Repro:** `tests/data/openc910/bug_cases/case_027` (DUT: axi_slave128).
- **Root causes:**
  1) **Always-true latch guard missed:** combinational always blocks with an
     explicit else-if/else chain were emitted as `always_latch` because the
     tautology in the update guard was not detected.
  2) **Sync read double-registering:** sync reads are modeled as
     `kMemoryReadPort + kRegister` (per docs). When the read was immediately
     assigned to a sequential target, the writeback path introduced a second
     `kRegister`, adding an unintended extra cycle.

## Fix Details
- **Emit always-true detection:** extend the latch guard check in `src/emit.cpp`
  to evaluate small boolean guard expressions via truth-table (<=8 leaves), and
  add `isConstZero` to avoid false positives. This switches tautological guards
  to `always @*` assignments.
- **Writeback demotion for pass-through sync reads:** keep sync reads as
  `kMemoryReadPort + kRegister` (per docs), but when a sequential write
  immediately forwards the sync-read output with matching updateCond/event,
  skip emitting the extra writeback register. The memory-read register drives
  the target directly, removing the unintended extra cycle.

## Files Changed
- `src/emit.cpp`
- `src/convert.cpp`
- `tests/data/openc910/bug_cases/case_027/tb_case_027.cpp`
- `tests/data/openc910/bug_cases/case_027/Makefile`
- `tests/data/openc910/bug_cases/case_027/coverage_check.py`
- `tests/data/openc910/bug_cases/case_027/bug_report.md`

## Validation
- `make -C tests/data/openc910/bug_cases/case_027 run_c910_bug_case_ref`
  - Coverage: **90.06%** (308/342)
- `make -B -C tests/data/openc910/bug_cases/case_027 run_c910_bug_case`
  - Coverage: **93.25%** (221/237)

## Notes / Follow-ups
- No global CTest run executed for this change.
- If any other memory models rely on the previous sync-read register insertion,
  re-evaluate with targeted TBs. The new behavior matches Verilog semantics
  for `mem[addr]` reads inside sequential blocks.
