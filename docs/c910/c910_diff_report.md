# C910 Diff Report

## 2026-02-06 13:50
- Command: `rg`/`diff` against existing logs
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_112250.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_112250.log
- Equivalence: Not equivalent
- Evidence:
  - wolf log only shows "no instructions retired" error
  - ref log contains CoreMark results and success banner
- Hypotheses:
  - CPU not retiring (reset/clock gating, fetch blocked, or X-propagation)
  - Possible width/sign issues in emit affecting control flow
- Instrumentation changes: none
- Results/next steps:
  - Add retirement + bus activity probes to compare ref vs wolf at 50k-cycle checkpoints

## 2026-02-06 14:05
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_135856.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_135856.log
- Equivalence: Not equivalent
- Evidence:
  - ref `[c910-diff]` shows retire_cnt > 25k and non-zero awaddr values
  - wolf `[c910-diff]` at cycle 50k shows retire_cnt=0, awaddr=0, wvalid=0
  - wolf still hits "no instructions retired" error, no CoreMark output
- Hypotheses:
  - Core reset deasserted but no bus writes; fetch/reads may be blocked
  - Outputs could be disconnected in emitted design (convert warns about skipped
    unconnected output ports in rv_integration_platform)
- Instrumentation changes:
  - Added `[c910-diff]` checkpoint logging in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
  - Removed unused probe in `tests/data/openc910/smart_run/logical/tb/tb_verilator.v`
- Results/next steps:
  - Expose read address/valid and core reset to confirm fetch path activity

## 2026-02-06 14:12
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_140651.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_140651.log
- Equivalence: Not equivalent
- Evidence:
  - ref `[c910-diff]` shows core_pad_rst_b=1 and non-zero araddr (arvalid toggles)
  - wolf `[c910-diff]` at cycle 50k shows core_pad_rst_b=1 but araddr=0/arvalid=0
  - wolf still fails with "no instructions retired" error
- Hypotheses:
  - Fetch path from core to BIU is not issuing reads in wolf emit
  - Signal disconnect or gating in emitted SV (arvalid path) despite deasserted reset
  - Missing/incorrect initialization could keep IFU from requesting reads
- Instrumentation changes:
  - Added `biu_pad_araddr`, `biu_pad_arvalid`, and `core_pad_rst_b` outputs in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Logged new signals in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Probe BIU/IFU request enable path (e.g., `biu_pad_arvalid` source) in
    `logical/common/rv_integration_platform.v` or `logical/common/cpu_sub_system_axi.v`
  - Compare wolf-emitted connections for arvalid path against ref

## 2026-02-06 14:20
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_142059.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_142059.log
- Equivalence: Not equivalent
- Evidence:
  - ref `[c910-diff]` shows `lpmd=0x3`, `no_retire=0`, non-zero araddr/awaddr
  - wolf `[c910-diff]` shows same `lpmd=0x3`/`no_retire=0` but ar/aw signals
    stay zero at 50k cycles
- Hypotheses:
  - Core clocks run but fetch never asserts, possibly reset gating or missing
    bus request propagation inside openC910
- Instrumentation changes:
  - Added `biu_pad_awaddr`, `biu_pad_awvalid`, `biu_pad_lpmd_b`,
    `pad_cpu_sys_cnt`, and `core0_cpu_no_retire` outputs in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Logged new signals in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Tap IBIU arvalid/araddr inside openC910 to see if any internal read request

## 2026-02-06 14:27
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_142727.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_142727.log
- Equivalence: Not equivalent
- Evidence:
  - ref `[c910-diff]` shows `ibiu0_arvalid` pulses (e.g., at cycle 200k)
  - wolf `[c910-diff]` shows `ibiu0_arvalid=0`, `ibiu1_arvalid=0` at 50k
- Hypotheses:
  - Reset gating inside openC910 (cpurst_b) stuck low in wolf emit
- Instrumentation changes:
  - Added `ibiu0_pad_araddr/valid` and `ibiu1_pad_araddr/valid` outputs in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Logged new signals in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Probe internal reset signals in openC910 (cpurst_b, core0_fifo_rst_b)

## 2026-02-06 14:40
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_143337.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_143337.log
- Equivalence: Not equivalent
- Evidence:
  - ref `[c910-diff]` shows `cpurst_b=1`, `core0_fifo_rst_b=1`
  - wolf `[c910-diff]` shows `cpurst_b=0` while `core0_fifo_rst_b=1` and
    `core_pad_rst_b=1`
  - Emitted `ct_mp_rst_top` in `tests/data/openc910/smart_run/work_wolf/wolf_emit/sim_top_wolf.sv`
    is missing `assign cpurst_b = pad_yy_scan_mode ? pad_yy_scan_rst_b : cpurst_3ff;`
    (only `apbrst_b` is assigned from `cpurst_3ff`)
- Hypotheses:
  - wolf-sv-parser drops or misbinds the `cpurst_b` assignment in
    `ct_mp_rst_top`, keeping CPU reset asserted
- Instrumentation changes:
  - Added `cpurst_b` and `core0_fifo_rst_b` outputs in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Logged new signals in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Inspect wolf-sv-parser emit/transform for ternary assigns in
    `ct_mp_rst_top` and fix missing `cpurst_b` assignment
