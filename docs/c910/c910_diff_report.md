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

## 2026-02-06 23:39
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_233154.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_233154.log
- Equivalence: Not equivalent
- Evidence:
  - `axi_slave128` shows identical control/addr but different memory data early:
    - ref cycle 2130: `mem_addr=0x10`, `mem_cen=1`,
      `rdata_lo=0x7c01a073004001b7`, `rdata_hi=0x01b73001a0736199`
    - wolf cycle 2130: `mem_addr=0x10`, `mem_cen=1`,
      `rdata_lo=0x61a13001a0730080`, `rdata_hi=0x006381b77c01a073`
  - `c910-window` still shows matching BIU read data (`biu_rdata_*`) but
    `rdata_s0_*` diverges in wolf at cycle 2131-2134, matching the
    `axi_slave128` dump above
  - `c910-window2` (cycle ~2547) shows PC stuck at 0x4c, no retire, and no
    active IFU/BIU read traffic in wolf while `rdata_s0_*` continues to show
    differing memory contents
  - Wolf fails at cycle 100000 with the "no instructions retired in the last
    50000 cycles" error; ref completes CoreMark
- Hypotheses:
  - Wolf corrupts memory contents during tb_init (byte/word ordering or slice
    mapping in `f_spsram_large`), so instruction fetch reads the wrong words
  - A parser bug in part-select arithmetic in `f_spsram_large` or `ram` wiring
    swaps lanes, causing the persistent `rdata_s0` mismatch even at the same
    address
  - Less likely: `tb_init_*` port connections differ in the emitted wolf SV
- Instrumentation changes:
  - Added a debug cycle counter and `$display` in
    `tests/data/openc910/smart_run/logical/axi/axi_slave128.v` to log
    `mem_addr/mem_wen/mem_dout` for cycles 2130-2145 and 2480-2520
  - Added a second window (2450-2550) with `c910-window2` tagging in
    `tests/data/openc910/smart_run/logical/tb/tb_c910.cpp`
- Results/next steps:
  - Instrument `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v` to
    log `tb_init_addr/tb_init_wdata` for a small address range (e.g. 0x0-0x20),
    and the `ram*_din` slices, to verify byte/word ordering during init
  - If init data matches but `rdata_lo/hi` still diverge, inspect `ram.v` or the
    emitted `sim_top_wolf.sv` for incorrect part-select lowering

## 2026-02-06 23:50
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_234248.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_234248.log
- Equivalence: Not equivalent
- Evidence:
  - `f_spsram_large` init writes match between ref and wolf for low addresses:
    - addr 0x0: `wdata=0x01b73001a07361997c01a073004001b7`
    - addr 0x1: `wdata=0x006381b77c01a07361a13001a0730080`
    (matching `ram0..ram3` bytes in both logs)
  - Despite identical init, wolf still shows `rdata_s0_*` divergence at
    cycle 2131 and stalls later with no retire
- Hypotheses:
  - Read path/addressing after init diverges (e.g., `addr_holding` or `addr`
    selection in `f_spsram_large`), leading to different `Q` outputs
  - Wolf’s emitted memory/array handling in `ram` or `f_spsram_large` alters
    read behavior even with identical write data
- Instrumentation changes:
  - Added `[spsram-init]` logging in
    `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v` for init writes
    (addr 0x0-0x1f) with `wdata/mem_d/ram0..ram3`
- Results/next steps:
  - Add post-init logging in `f_spsram_large` for early read accesses
    (addr 0x0/0x1), capturing `mem_addr`, `addr_holding`, `addr`, and `Q`
  - If read addresses match but `Q` differs, instrument `ram.v` to log
    `PortAAddr/PortADataOut` for a single instance via `%m` filtering

## 2026-02-07 00:00
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260206_235256.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260206_235256.log
- Equivalence: Not equivalent
- Evidence:
  - Post-init read traces diverge immediately:
    - ref: `mem_addr=0x000001` with `addr_hold=0x000000` yields
      `Q=0x01b73001a07361997c01a073004001b7` (line 0 data)
    - ref: `mem_addr=0x000000` with `addr_hold=0x000001` yields
      `Q=0x006381b77c01a07361a13001a0730080` (line 1 data)
    - wolf: `mem_addr=0x000000` with `addr_hold=0x000002` yields
      `Q=0x42014181410140817f30e0737c01a073` (line 2 data)
  - This indicates the address stream in wolf is already advanced to line 2
    before the first window; ref alternates line 0/1 as expected
- Hypotheses:
  - Wolf’s address/update sequencing in `axi_slave128` (mem_addr update or
    wrap logic) differs, advancing the line index early
  - The emitted design’s ordering of mem_addr/addr_holding updates is altered,
    causing a read pipeline offset even though init data matches
- Instrumentation changes:
  - Narrowed init logging to addr < 0x2 and added post-init `[spsram-read]`
    logging with `mem_addr/addr_holding/addr/Q` in
    `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`
- Results/next steps:
  - Instrument `tests/data/openc910/smart_run/logical/axi/axi_slave128.v` to
    log mem_addr update reasons (arvalid/awvalid/read_step/wrap flags) in the
    post-init window and compare ref vs wolf

## 2026-02-07 00:59
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_005159.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_005159.log
- Equivalence: Not equivalent (wolf halts with no retired instructions)
- Evidence:
  - `[axi-memaddr]` traces are identical between ref and wolf (diff of
    filtered logs shows no differences).
  - `[spsram-read]` still diverges immediately:
    - ref: `mem_addr=0x000000` with `addr_hold=0x000001` yields
      `Q=0x006381b77c01a07361a13001a0730080`
    - wolf: `mem_addr=0x000000` with `addr_hold=0x000002` yields
      `Q=0x42014181410140817f30e0737c01a073`
- Hypotheses:
  - The mismatch is not in `axi_slave128` mem_addr sequencing; instead it is
    likely in `f_spsram_large` address-hold or read pipeline timing (addr_hold
    is +1 in wolf during the first post-init reads).
  - Wolf emit may be reordering or lowering the address-holding logic
    differently from the ref simulator.
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/axi/axi_slave128.v`:
    - Switched `[axi-memaddr]` filter to `mem_addr[24:4] < 4` and added
      `mem_loc` to match spsram local addressing.
    - Made `init_seen` latch after reset so post-init logging triggers even if
      `tb_init_en` pulses during reset.
- Results/next steps:
  - Add targeted logging in `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`
    when `addr_holding` updates to capture the exact cycle and gating
    (`mem_cen`, `mem_addr`, `addr_holding`, `addr`), then compare ref vs wolf.

## 2026-02-07 01:29
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_012325.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_012325.log
- Equivalence: Not equivalent (wolf halts with no retired instructions)
- Evidence:
  - `spsram-post` shows `addr_holding` changes in wolf even when `mem_cen=1`:
    - ref: `post=0..15` keeps `addr_hold=0x007fff` with `mem_cen=1`
    - wolf: `post=0` has `addr_hold=0x007fff`, but `post=1` drops to
      `addr_hold=0x000000` while `mem_cen=1` and `A=0x000000`
  - This explains why early `spsram-hold` entries show `addr_hold_prev`
    already advanced in wolf.
- Hypotheses:
  - The wolf-emitted model is not preserving the sequential “hold” semantics
    of `addr_holding` when `mem_cen` is high (likely a lowering/optimization
    issue around the `if(!mem_cen)` block in `f_spsram_large.v`).
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`:
    - Added `last_init_active` and `[spsram-init-done]` logging to capture
      end-of-init state.
    - Added `[spsram-post]` logging for the first 16 post-init cycles with
      `mem_addr`, `mem_cen`, `addr_holding`, `CEN`, and `A`.
    - Extended `[spsram-hold]` to include `mem_addr==0x07fff`.
- Results/next steps:
  - Inspect `work_wolf/wolf_emit/sim_top_wolf.sv` for the lowered
    `addr_holding` always block and compare to ref to confirm an unintended
    assignment when `mem_cen` is high.
  - As an experiment, add an explicit `else addr_holding <= addr_holding;`
    in `f_spsram_large.v` to see if wolf behavior matches ref (indicating a
    converter issue with “implicit hold” semantics).

## 2026-02-07 01:33
- Command: inspected `tests/data/openc910/smart_run/work_wolf/wolf_emit/sim_top_wolf.sv`
- Logs: (no new run)
- Equivalence: Not equivalent (same as prior)
- Evidence:
  - In wolf-emitted `f_spsram_large`, the addr_holding update lost its guard:
    - `always @(posedge CLK) begin addr_holding <= __expr_4; end`
      (around `sim_top_wolf.sv:13483`)
  - The original RTL uses `if(!mem_cen) addr_holding <= mem_addr;` so the
    unconditional assignment explains why wolf changes `addr_holding` even
    when `mem_cen=1`.
- Hypotheses:
  - The converter/optimizer is incorrectly dropping the `if(!mem_cen)` guard
    on sequential assignments, causing the observed read-address skew.
- Instrumentation changes: (none)
- Results/next steps:
  - Try adding an explicit `else addr_holding <= addr_holding;` in
    `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v` to see if the
    wolf-emitted RTL preserves the guard and aligns behavior.

## 2026-02-07 02:06
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_015954.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_015954.log
- Equivalence: Not equivalent (ref finishes CoreMark; wolf still stalls)
- Evidence:
  - ref: `VCUNT_SIM: CoreMark has been run 2 times...` and `CoreMark Size : 666`
  - wolf: `* Error: There is no instructions retired in the last 50000 cycles! *`
  - `spsram-post` now shows `addr_hold=0x007fff` across post=0..15 with `mem_cen=1`
    (the explicit hold appears to keep the guard in behavior), but the stall persists.
- Hypotheses:
  - The remaining mismatch is outside `f_spsram_large` hold semantics; likely an
    instruction-fetch or AXI read-data path issue (e.g., `axi_slave128`/`axi_fifo`
    read data/valid gating or IFU/BIU handshake divergence).
  - Missing `[spsram-read]` lines early in the wolf log suggest read-back logging
    is suppressed or reads are not occurring as in ref; a data path mismatch may
    still exist even with correct `addr_holding` gating.
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`:
    - Added explicit `else addr_holding <= addr_holding;` to preserve the hold.
- Results/next steps:
  - Add minimal probes in `tests/data/openc910/smart_run/logical/axi/axi_slave128.v`
    to log `arvalid/rvalid/rdata` for early reads and compare ref vs wolf.
  - If needed, add a small fixed-count `[spsram-read]` print (first N reads)
    to confirm identical Q values between ref and wolf.

## 2026-02-07 02:18
- Command: (no run)
- Logs: (no new run)
- Equivalence: Not re-evaluated
- Evidence:
  - Reverted the explicit hold experiment in `f_spsram_large` to restore the
    original RTL semantics before further localization.
- Hypotheses:
  - None (preparing for the next targeted probe).
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`:
    - Removed the explicit `else addr_holding <= addr_holding;` assignment.
- Results/next steps:
  - Proceed with targeted probes in the AXI read/IFU path.

## 2026-02-07 02:26
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_022007.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_022007.log
- Equivalence: Not equivalent (ref finishes CoreMark; wolf stalls)
- Evidence:
  - ref: `VCUNT_SIM: CoreMark has been run 2 times...` / `CoreMark Size : 666`
  - wolf: `* Error: There is no instructions retired in the last 50000 cycles! *`
  - `spsram-post` diverges immediately after post=0:
    - ref keeps `addr_hold=0x007fff` for post=0..4
    - wolf drops to `addr_hold=0x000000` at post=1
  - Early `[axi-read]` samples match between ref and wolf for the first 10 reads
    (same `araddr`, `mem_addr`, and `rdata` values).
- Hypotheses:
  - The AXI read data path looks consistent for early reads; the stall is likely
    driven by the `addr_holding` divergence in `f_spsram_large` (guard dropped in
    wolf emit) or a later read/IFU path interaction.
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/axi/axi_slave128.v`:
    - Added `rd_dbg_cnt` and `[axi-read]` prints on early `rvalid && rready_s0`
      handshakes (first 16 reads, SRAM-local window).
- Results/next steps:
  - Re-check wolf-emitted `f_spsram_large` to confirm the `addr_holding` guard is
    being dropped again when the explicit hold is absent.
  - If confirmed, keep the explicit hold temporarily and re-run to see whether the
    stall persists even with aligned `addr_hold`, then pivot to IFU/BIU probes.

## 2026-02-07 09:52
- Command: `rg`/`sed` on `tests/data/openc910/smart_run/work_wolf/wolf_emit/sim_top_wolf.sv`
- Logs: (no new run)
- Equivalence: Not re-evaluated
- Evidence:
  - In wolf-emitted `f_spsram_large`, the `addr_holding` assignment is unconditional:
    `always @(posedge CLK) begin addr_holding <= __expr_4; end` (no `if(!mem_cen)` gate).
  - The conditional `if(!mem_cen)` guard from the original RTL is absent in the emitted
    `addr_holding` update, while the debug `$display` remains guarded.
- Hypotheses:
  - Wolf emit/scheduling is splitting the original guarded always block and dropping
    the `if(!mem_cen)` condition on the state update, causing `addr_holding` to track
    `mem_addr` even when `mem_cen=1`.
- Instrumentation changes:
  - None (inspection only).
- Results/next steps:
  - If you want, I can proceed to fix the emitter to preserve the guard and rerun
    `make run_c910_diff -j` to confirm equivalence, or add more AXI read-path probes
    before touching the emitter.
