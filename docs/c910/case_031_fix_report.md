# C910 Case 031 Fix Report

## Problem summary

`ct_fifo` failed in wolf emit when a create pulse was followed by a hold cycle. The bugcase reported:

```
[TB] fifo_pop_data mismatch: got=0x0 expected=0x20
```

## Minimal repro

- Case: `tests/data/openc910/bug_cases/case_031`
- DUT top: `ct_fifo`
- RTL: `ct_fifo.v` + `gated_clk_cell.v`
- Coverage (RTL ref): 93.75% (60/64)

## Root cause analysis

The memory port lowerer treated any memory read used inside a sequential statement as a synchronous read. For `ct_fifo`, the self-hold path `fifo_entry_cont[i] <= fifo_entry_cont[i];` was lowered into a sync read register (`__mem_data_*`). When `fifo_entry_create_dp` deasserted after a create pulse, the read register lagged by one cycle and wrote the stale value back into `fifo_entry_cont`, overwriting the newly created entry and causing the observed mismatch.

## Fix details

### Approach

- Detect sequential memory reads that feed memory writes to the same memory (read-modify-write or self-hold).
- Treat these reads as combinational: clear sync metadata so they emit as direct `assign` reads instead of `mem_read_reg`.

### Code changes

- `src/convert.cpp`: convert memory reads used by memory writes to combinational reads, with a post-pass over `memoryWrites` to catch read-modify-write usage reliably.
- `tests/data/convert/memory_ports.sv`: add `mem_read_seq_self_hold` fixture.
- `tests/convert/test_convert_memory_port_lowerer.cpp`: add a unit test for the self-hold read classification.

## Validation

- Reference RTL run:
  - `make -C tests/data/openc910/bug_cases/case_031 run_c910_bug_case_ref`
  - Coverage: 93.75% (60/64)
- Wolf emit run:
  - `make -C tests/data/openc910/bug_cases/case_031 run_c910_bug_case`
  - Coverage: 95.95% (71/74)
- Targeted unit test:
  - `ctest --test-dir build --output-on-failure -R convert-memory-port-lowerer`

## Risks and follow-ups

- The change alters sync classification only for reads used by memory writes; other sequential reads remain sync. If additional read-modify-write patterns appear in other modules, they should now be handled correctly.
