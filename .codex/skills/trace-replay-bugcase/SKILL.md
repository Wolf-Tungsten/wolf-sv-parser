---
name: trace-replay-bugcase
description: Create minimal trace-replay bugcases from FST waveforms using a module path or signal name to extract inputs and replay them. Use when isolating ref vs wolf diffs, missing internal waves, or when you want a small confirmation case without rerunning full simulations.
---

# Trace Replay Bugcase

## Overview
Build a minimal replay test by extracting signal traces from FST waves and driving a small DUT. This avoids full-system reruns while preserving real stimulus around a failing window.

## Inputs
- FST path(s) and the time window (`t0`, `t1`) to replay.
- A module instance path or a signal name/glob.
- Clock/reset signal names (for cycle alignment).

## Workflow
1) Identify the FST and time window.
   - Prefer the first mismatch window (e.g., `t0=16550 t1=16620`).
   - Keep the window small to reduce stimulus size.

2) Resolve the DUT interface.
   - If you have a **module instance path** (e.g., `TOP....grantBuf`):
     - List signals in the module:
       - `python3 .codex/skills/trace-replay-bugcase/scripts/fst_trace_export.py --fst <fst> --list --list-filter "<instance_path>."`
     - Open the RTL module definition and record **input ports** and widths.
       - Use `rg -n "module <ModuleName>" build/xs/rtl/rtl` and inspect the port list.
     - Build the signal list as `<instance_path>.<port_name>`.
   - If you have a **signal name/glob**:
     - Use it directly for trace extraction, and add clock/reset signals explicitly.

3) Extract stimulus traces (inputs only).
   - Use the bundled extractor:
     - `python3 .codex/skills/trace-replay-bugcase/scripts/fst_trace_export.py --fst <fst> --signals-file <signals.txt> --t0 <t0> --t1 <t1> --format jsonl --strip-width`
   - Keep the signals file minimal: clock/reset + DUT inputs + any gating signals needed for valid/ready.

4) Create the minimal bugcase.
   - Create a new case folder (e.g., `tests/data/xs-bugcase/CASE_00X/`).
   - Add `dut.sv` (the module under test) and a small `tb.sv` to replay the trace.
   - Replay strategy (simple and robust):
     - Drive inputs on the extracted timestamps.
     - Hold values until the next change (trace is sparse).

5) Validate.
   - Run the bugcase with ref vs wolf (or just wolf) to reproduce the mismatch.
   - If the mismatch disappears, widen the window or include additional inputs.

## Minimal replay TB pattern (SV)
Use a sparse event list to apply changes. This is intentionally simple; for large windows, switch to a C++ driver.

```systemverilog
// tb.sv (sketch)
`timescale 1ns/1ps
module tb;
  logic clk = 0;
  logic reset = 1;
  // DUT inputs
  logic [511:0] io_d_task_bits_data_data;
  logic io_d_task_valid;
  // ... other inputs

  // DUT outputs (wire)
  wire [255:0] io_d_bits_data;
  // ... other outputs

  // Clock
  always #1 clk = ~clk; // 2 time units per cycle

  // DUT
  GrantBuffer dut (.*);

  // Simple event replay (generated from JSONL)
  initial begin
    // apply reset
    repeat (5) @(posedge clk);
    reset = 0;
    // Insert events here: at time T, assign inputs
    // Example:
    // #16566 io_d_task_valid = 1'b1;
    // #16566 io_d_task_bits_data_data = 512'h...;
  end
endmodule
```

## Notes and heuristics
- Always include clock/reset, plus any valid/ready handshake inputs.
- If the DUT is a pipeline, include a few cycles of pre-roll before the mismatch.
- When the DUT is inside a larger slice, prefer the smallest module with a stable input bundle (e.g., `grantBuf` over `SourceD`).

## Resources
### scripts/
- `fst_trace_export.py`: Extract sparse time/value events for selected signals from an FST.
