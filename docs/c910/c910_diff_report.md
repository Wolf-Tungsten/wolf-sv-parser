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

## 2026-02-06 14:53
- Command: (code inspection)
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_143337.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_143337.log
- Equivalence: Not equivalent
- Evidence:
  - Emitted `ct_mp_rst_top` assigns `apbrst_b = pad_yy_scan_mode ? pad_yy_scan_rst_b : cpurst_3ff;`
    at the source location for `cpurst_b`, leaving `cpurst_b` undriven
- Hypotheses:
  - Output-assign inlining pass drops `cpurst_b` when it is only used by
    `apbrst_b`, even though `cpurst_b` is an output port
- Instrumentation changes:
  - None (analysis of `src/pass/redundant_elim.cpp`)
- Results/next steps:
  - Found in `src/pass/redundant_elim.cpp` that `kAssign` inlining replaces the
    defining op result with an output port without guarding against the source
    being another port. This can inline `cpurst_b` into `apbrst_b` and drop the
    `cpurst_b` driver. Fix: skip output-assign inlining when the source is any
    port value.

## 2026-02-06 15:10
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_150431.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_150431.log
- Equivalence: Not equivalent
- Evidence:
  - wolf `[c910-diff]` shows `cpurst_b=1` at 50k/100k cycles but still hits
    "There is no instructions retired in the last 50000 cycles" at 100k
  - ref run completes CoreMark successfully
- Hypotheses:
  - Reset issue fixed; remaining stall likely in fetch/IBIU path or another
    dropped assignment unrelated to `cpurst_b`
- Instrumentation changes:
  - None
- Results/next steps:
  - Inspect wolf emit around IBIU/arvalid path and compare against ref to
    find next missing assignment or optimization side-effect

## 2026-02-06 15:17
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_151750.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_151750.log
- Equivalence: Not equivalent
- Evidence:
  - wolf stops at 100k cycles with "no instructions retired"; ref completes CoreMark
  - wolf lacks CoreMark output lines
- Hypotheses:
  - IFU/BIU request activity may be missing or too sparse before failure
  - A request/valid signal might be dropped between openC910 and the BIU
- Instrumentation changes:
  - Exposed `ifu_biu_rd_req`, `ifu_biu_rd_req_gate`, `arvalid`, and `arvalid_gate` from
    `x_ct_biu_top` in `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Logged new signals in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Add per-period "any" tracking to confirm whether these signals ever pulse

## 2026-02-06 15:24
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_152437.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_152437.log
- Equivalence: Not equivalent
- Evidence:
  - ref shows `*_any=1` for IFU/BIU request signals every 50k window
  - wolf shows `*_any=1` in the first 50k window, but `*_any=0` in the next window
  - wolf still fails at 100k cycles with no retired instructions
- Hypotheses:
  - IFU/BIU request activity halts after early transactions in wolf
  - Potential stall due to missing handshake/response path after initial requests
- Instrumentation changes:
  - Added per-period "any" latches for IFU/BIU request/valid signals in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Probe response/ready signals (pad_biu_arready/pad_biu_rvalid)

## 2026-02-06 15:31
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_153123.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_153123.log
- Equivalence: Not equivalent
- Evidence:
  - ref shows `pad_arready_any=1` and `pad_rvalid_any=1` in every 50k window
  - wolf shows `pad_arready_any=1`/`pad_rvalid_any=1` in the first 50k window, then 0
    in the next window before the 100k-cycle failure
- Hypotheses:
  - Wolf run enters a post-initialization stall where BIU response/ready activity stops
  - Possible address width/truncation or dropped assignment in the BIU/IFU path
- Instrumentation changes:
  - Exposed `pad_biu_arready` and `pad_biu_rvalid` in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Logged current/"any" values in `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Track high-bit activity on `biu_pad_araddr`/`ibiu0_pad_araddr` per window to detect
    truncation
  - Inspect wolf emit for `biu_pad_araddr`/`ifu_biu_rd_addr` width handling in
    `ct_biu_top`/`ct_ifu` and any dropped assigns around those paths

## 2026-02-06 15:48
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_154234.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_154234.log
- Equivalence: Not equivalent
- Evidence:
  - ref prints 100 instruction-retire lines (`[c910-iret] idx=1..100`), continues to
    complete CoreMark
  - wolf prints only 17 instruction-retire lines then stalls; no more retires after
    cycle ~2350 and fails at 100k cycles
  - wolf first 17 PCs: 0x0, 0x4, 0x8, 0xe, 0x10, 0x12, 0x16, 0x1c, 0x20, 0x24,
    0x2a, 0x30, 0x36, 0x3c, 0x40, 0x46, 0x4c
- Hypotheses:
  - Wolf diverges during early boot/ROM execution and stops fetching after PC 0x4c
  - Potential dropped assignment or width issue on IFU fetch path after initial steps
- Instrumentation changes:
  - Added core0 retire PC outputs and per-instruction logging in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v` and
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Compare ref vs wolf around PC 0x4c: dump nearby fetch request signals per-cycle
    for a short window (e.g., cycles 2000-2600)
  - If needed, add temporary per-cycle trace for IFU/BIU request/ready around that
    window to see which handshake breaks immediately after the last retired PC

## 2026-02-06 15:56
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_155012.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_155012.log
- Equivalence: Not equivalent
- Evidence:
  - Per-cycle window (2000-2600) shows ref continues IFU/BIU requests through
    cycle 2594 (`ifu_req=1`, `biu_arvalid=1`) and `ibiu0_arvalid=1` through 2595
  - Wolf shows last `ifu_req=1`/`biu_arvalid=1` at cycle 2342 and last
    `ibiu0_arvalid=1` at cycle 2343; afterward the window is all zeros and PC
    remains 0x4c
  - Wolf still stops at 100k cycles with no retires; ref completes CoreMark
- Hypotheses:
  - Fetch request gating in wolf deasserts around cycle ~2343 (post-early boot)
  - Possible dropped assignment or gating condition in IFU/BIU request path
- Instrumentation changes:
  - Added per-cycle window log (cycles 2000-2600) in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Narrow to signals that gate `ifu_biu_rd_req` (e.g., IFU/CP0 enable or low-power
    controls) and compare ref vs wolf around cycle 2330-2350

## 2026-02-06 16:08
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_160109.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_160109.log
- Equivalence: Not equivalent
- Evidence:
  - Narrow window (2320-2360) shows only a single IFU request in each run:
    - ref: cycle 2340 `ifu_req=1`, `ifu_addr=0x30`, `biu_arvalid=1`; cycle 2341
      `ibiu0_arvalid=1`, `ibiu0_araddr=0x30`
    - wolf: cycle 2342 `ifu_req=1`, `ifu_addr=0x50`, `biu_arvalid=1`; cycle 2343
      `ibiu0_arvalid=1`, `ibiu0_araddr=0x50`
  - cp0_biu_icg_en=0 and cp0_biu_lpmd_b=0x3 match across ref/wolf in the window
  - wolf still halts after PC 0x4c and fails at 100k cycles; ref keeps retiring
- Hypotheses:
  - Wolf PC/IFU address diverges before the stall (0x50 vs 0x30 fetch), possibly due
    to a missing retire or incorrect branch/exception flow before cycle ~2340
  - IFU/BIU handshake completes the request, but follow-on response/retire diverges
- Instrumentation changes:
  - Added IFU/BIU request/response signals (addr/len/size/id, grant, data_vld/resp)
    and CP0 BIU gating signals in `tests/data/openc910/smart_run/logical/tb/sim_top.v`
  - Tightened per-cycle window to cycles 2320-2360 and expanded log contents in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Widen the window slightly around 2336-2355 but add IFU response data/last/resp
    correlation to see if the 0x50 fetch in wolf receives data
  - If response arrives, compare the decoded instruction stream around PC 0x30/0x50
    to determine why wolf advances PC differently

## 2026-02-06 16:20
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_161337.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_161337.log
- Equivalence: Not equivalent
- Evidence:
  - ref completes CoreMark; wolf still halts at 100k cycles with no retire
  - Retire divergence appears very early: ref retires PC 0xa at cycle 2152 (idx=4),
    while wolf skips 0xa (idx=4 is PC 0xe at cycle 2144) and adds PC 0x10 at 2168
  - Window 2320-2360 shows divergent PCs before the stall:
    - ref: cycle 2320 `pc0=0x1c`, `ifu_mmu_va=0x10`
    - wolf: cycle 2320 `pc0=0x2a`, `ifu_mmu_va=0x20`
  - IFU fetch address differs in the window:
    - ref: cycle 2340 `ifu_req=1`, `ifu_addr=0x30`, `biu_arvalid=1`
    - wolf: cycle 2342 `ifu_req=1`, `ifu_addr=0x50`, `biu_arvalid=1`
- Hypotheses:
  - Divergence originates around PC 0xa/0x10 (cycles ~2140-2170), not at the later
    0x30/0x50 fetch; IFU stream is already off by then
  - IFU/BIU data stream differs (biu_rdata_lo differs in the window), possibly due
    to incorrect fetch address/PC selection or missing retire/exception handling
- Instrumentation changes:
  - Expanded window logging to include IFU status + MMU signals and HAD IR/PC in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
  - Added IFU/HAD + MMU/IFU status and BIU read response data/resp signals in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v`
- Results/next steps:
  - Move the window earlier (around cycles 2130-2185) to capture the first
    divergence at PC 0xa/0x10 with the same signal set
  - If needed, add BIU read data upper 64 bits or a 32-bit fetch decode to verify
    the exact instruction stream around 0x8/0xa/0xe

## 2026-02-06 16:32
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_162529.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_162529.log
- Equivalence: Not equivalent
- Evidence:
  - Earliest divergence is the BIU read data while PC/addresses match:
    - cycle 2134: ref `biu_rdata_lo=0x7c01a073004001b7`,
      `biu_rdata_hi=0x1b73001a0736199`
    - cycle 2134: wolf `biu_rdata_lo=0x7f30e0737c01a073`,
      `biu_rdata_hi=0x4201418141014081`
  - Cycles 2130-2133 are identical between ref/wolf; PC=0x4, IFU/BIU request
    signals match; divergence starts at the read data beat
  - Wolf still stalls after PC 0x4c and fails at 100k cycles; ref completes
    CoreMark
- Hypotheses:
  - Wolf corrupts or misroutes IFU read data (BIU->IFU path) starting at cycle
    2134, leading to incorrect instruction stream at PC 0xa/0x10
  - If pad_biu_rdata already differs, memory init or prior store path diverges;
    if pad_biu_rdata matches, the issue is inside BIU/IFU data handling
- Instrumentation changes:
  - Moved window to cycles 2130-2185 in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
  - Added `biu_ifu_rd_data_hi` probe in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v` and logging in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Add `pad_biu_rdata[127:0]` and log it alongside `biu_ifu_rd_data_*` to see
    whether the mismatch originates at the external BIU read data or inside BIU/IFU

## 2026-02-06 16:45
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_164449.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_164449.log
- Equivalence: Not equivalent
- Evidence:
  - Cycle 2134 shows the first BIU read data divergence and now exposes the full
    data path taps:
    - ref: `biu_rdata_lo=0x7c01a073004001b7`,
      `biu_rdata_hi=0x1b73001a0736199`
    - wolf: `biu_rdata_lo=0x7f30e0737c01a073`,
      `biu_rdata_hi=0x4201418141014081`
    - ref: `rdata_s0_lo/rdata_s0_hi` match `biu_rdata_*`
    - wolf: `rdata_s0_lo/rdata_s0_hi` differ (`0x61a13001a0730080` /
      `0x6381b77c01a073`) and do not match `biu_rdata_*`
  - `pad_rdata_lo/hi=0x0` and `rvalid_s0=0` in this window, while
    `biu_rvld=1`, indicating BIU sees a valid beat while interconnect outputs are
    not asserting valid in the same cycle
  - `arvalid_s0..s3=0` and `fifo_araddr=0x0` during the window, so the visible
    interconnect AR handshakes are not active at the divergence point
- Hypotheses:
  - Wolf already corrupts the axi_slave128 data (`rdata_s0`) by cycle 2132-2134
    (memory contents differ), or the BIU is sourcing data from a different path
    than `rdata_s0`
  - The BIU->IFU data path in wolf is no longer aligned with interconnect outputs
    (biu_rvld asserted without corresponding rvalid_s0)
- Instrumentation changes:
  - Added full data path taps for `pad_biu_rdata`, `rdata_s0..s3`, `rvalid_s0..s3`,
    `rresp_s0..s3`, `arvalid_s0..s3`, and `fifo_pad_araddr/arvalid` in
    `tests/data/openc910/smart_run/logical/tb/sim_top.v` and
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - No further signals needed for the IFU read data path; next step is to inspect
    why `rdata_s0` diverges (axi_slave128 memory model) or why BIU uses data that
    does not match `rdata_s0` in wolf
