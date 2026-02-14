# XS Waveform Diff Diagnosis (2026-02-13)

## Executive Summary
- The ref run reaches cycle 5000, while the wolf run aborts at cycle 0 with an assertion, so the divergence happens at initialization.
- `tools/fst_diff_tool.py` reports the earliest differences at t=0 on multiple `gatewayIn_packed_*` signals (difftest packed buses).
- The primary cause is that wolf emit drops the `ENABLE_INITIAL_REG_` / `RANDOMIZE_REG_INIT` `initial` blocks. In `ICacheMainPipe`, this removes random initialization for `s1_datas_r_*`, so `difftest_refill_data_*` becomes 0/default at t=0, while ref has randomized non-zero data. This immediately produces waveform mismatches.
- Secondary symptom: `PrintCommitIDModule` format strings are converted to dynamic `string` variables in wolf emit; Verilator does not apply `%d/%h` formatting to dynamic format strings, so the log shows literal `%d/%h` text.

## Log Evidence
- Ref runs to cycle limit: `build/logs/xs/xs_ref_20260213_171229.log:1-12`.
- Wolf aborts at cycle 0 with `MEFreeList.sv:2032` assertion and `intFreeList` errors: `build/logs/xs/xs_wolf_20260213_171229.log:1-18`.

## FST Evidence (fst_diff_tool)
Command used:
```
python3 tools/fst_diff_tool.py \
  --fst-a build/logs/xs/xs_ref_20260213_171229.fst \
  --fst-b build/logs/xs/xs_wolf_20260213_171229.fst \
  --top 5 --format table
```
Earliest diffs are at t=0 and include:
- `TOP.SimTop.cpu.gatewayIn_packed_0_bore`
- `TOP.SimTop.cpu.gatewayIn_packed_100_bore`
- `TOP.SimTop.cpu.gatewayIn_packed_102_bore`
- `TOP.SimTop.cpu.gatewayIn_packed_103_bore`
- `TOP.SimTop.cpu.gatewayIn_packed_104_bore`

These are difftest packed buses, and an immediate t=0 mismatch indicates initialization divergence.

## Source vs Wolf Emit Comparison
### 1) ICacheMainPipe random init removed
Ref SV includes random initialization under `ENABLE_INITIAL_REG_` / `RANDOMIZE_REG_INIT`:
- `build/xs/rtl/rtl/ICacheMainPipe.sv:720-766`

Wolf emit declares the same registers but does not include any `initial` randomization block:
- `build/xs/wolf/wolf_emit/wolf_emit.sv:1247868-1248440`

This impacts difftest refill data packing:
- Ref packing: `build/xs/rtl/rtl/ICacheMainPipe.sv:904-917`
- Wolf packing: `build/xs/wolf/wolf_emit/wolf_emit.sv:1249161-1249163`

### 2) $fwrite format string becomes dynamic
Ref uses a literal format string:
- `build/xs/rtl/rtl/PrintCommitIDModule.v:8-11`

Wolf emit stores the format string in a `string` assigned by `always_comb`:
- `build/xs/wolf/wolf_emit/wolf_emit.sv:7633898-7633912`

Verilator does not format dynamic strings, explaining the `%d/%h` artifacts in the wolf log.

## Root Cause
- Wolf emit drops `initial` randomization blocks (consistent with convert warnings about skipping non-edge-sensitive sequential writes), leaving many registers at 0/default at t=0.
- In `ICacheMainPipe`, this makes `s1_datas_r_*` zero at t=0, propagating to `difftest_refill_data_*` and then `gatewayIn_packed_*`, causing immediate waveform differences.
- The inconsistent initial state triggers `MEFreeList` assertions before any meaningful execution begins.

## Suggested Fix Directions
1) Preserve `initial` blocks under `ENABLE_INITIAL_REG_` / `RANDOMIZE_REG_INIT` in wolf emit, or emit equivalent random-init logic.
2) Keep system-task format strings as literals (do not rewrite into dynamic `string` variables).
3) Re-run `fst_diff_tool.py` after fixes to confirm t=0 diffs are gone and the wolf sim progresses beyond cycle 0.
