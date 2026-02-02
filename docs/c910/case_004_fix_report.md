case_004 fix report
===================

problem summary
---------------
- original failure: "Unknown hierarchical symbol in expression" when parsing
  `tests/data/openc910/smart_run/logical/tb/sim_top.v` (log: `build/artifacts/c910_run.log`).
- example: `assign retire0 = x_soc...core0_pad_retire0;` in `sim_top.v`.

minimal repro
-------------
- case: `tests/data/openc910/bug_cases/case_004`
- dut top: `sim_top`
- filelist: `filelist.f` (local `sim_top.v` + `stub_modules.v`)
- note: `run_c910_bug_case_ref` passes with coverage above 90% (last recorded ~93%).

root cause
----------
- GRH conversion did not model hierarchical references as first-class dataflow.
- hierarchical paths were treated as plain symbols, so lookup failed for
  `x_soc.x_cpu_top...core0_pad_retire0` during conversion.

fix details
-----------
- `include/grh.hpp` / `src/grh.cpp`: add `kXMRRead` and `kXMRWrite` ops.
- `src/convert.cpp`: record hierarchical references as XMR ops (read/write) with
  `xmrPath` metadata; conversion no longer resolves dotted names locally.
- `src/pass/xmr_resolve.cpp`: new XMR resolve pass that adds ports, rewires
  instance ops, and connects leaf signals so XMR is expressed as normal GRH
  connections before emit.
- `src/emit.cpp`: no longer tolerates dotted identifiers; unresolved XMR ops now
  emit an error to ensure the resolve pass ran.

validation
----------
- `make -C tests/data/openc910/bug_cases/case_004 run_c910_bug_case COV_MIN=0`
  - ran without the original "Unknown hierarchical symbol" error.
  - coverage reported 87.74% (136/155) for `wolf_emit.sv`.

open follow-ups
--------------
- wolf coverage is below 90% due to non-executable declaration lines in the
  emitted SV; not addressed per user request.
- XMR writes are resolved only for combinational/unconditional cases; sequential
  or conditional XMR writes are currently skipped with warnings.
