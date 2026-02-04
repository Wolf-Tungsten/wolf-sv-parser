# case_016 Fix Report

## Problem summary
- Wolf-emitted SV introduced `__xmr_r_*` output ports on `ct_top` to resolve hierarchical reads but only connected them on the referenced instance (`x_ct_top_0`).
- Sibling instances (`x_ct_top_1`) were left without the new port connections, triggering Verilator `PINMISSING` warnings.

## Failure signature
```
%Warning-PINMISSING: .../sim_top_wolf.sv:3848:10: Instance has missing pin: '__xmr_r_576c484e999f5a56'
... Location of port declaration ... output wire [63:0] __xmr_r_576c484e999f5a56
```

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_016`
- DUT top: `sim_top`
- RTL sources: `sim_top.v`, `stub_modules.v`
- Ref coverage: not run (Verilator not invoked in this session)

## Root cause analysis
- `XmrResolvePass::resolveRead` adds new output ports to the referenced module graph and only updates the instance on the XMR path.
- When the same module has additional instances, those instances are not updated with the new port names/results, so the emitted SV instantiation omits the newly-added port.

## Fix details
- Track newly-added XMR input/output ports and propagate them to **all** instances of the affected module across the netlist.
- For input ports (XMR writes), connect missing instances to a zero pad constant to avoid missing pins.
- Files changed: `src/pass/xmr_resolve.cpp`.
- Added repro case: `tests/data/openc910/bug_cases/case_016`.

## Validation
- `cmake --build build --target wolf-sv-parser -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_016 run`
- Not run: `run_c910_bug_case_ref` / `run_c910_bug_case` (Verilator not invoked in this session).

## Follow-ups / risks
- Consider whether XMR write padding should use a different default than zero for multi-instance modules.
- Run Verilator on case_016 to confirm the `PINMISSING` warning is gone in wolf output.
