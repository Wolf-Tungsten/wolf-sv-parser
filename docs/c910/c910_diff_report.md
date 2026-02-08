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

## 2026-02-07 10:48
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_103929.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_103929.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - ref: `VCUNT_SIM: CoreMark has been run 2 times...` / `CoreMark Size    : 666`
  - wolf: `* Error: There is no instructions retired in the last 50000 cycles! *`
  - `[spsram-hold-breach]` appears once at post=0 in both logs (expected first
    update after init), with no further breaches during the first 5000 cycles.
  - wolf at cycle 100000: `ifu_req_any=0`, `biu_arvalid_any=0`,
    `pad_arready_any=0` (no IFU/BIU activity in that window).
- Hypotheses:
  - The stall is likely in the IFU/BIU request gating path (AR valid generation
    or gating dropping out), not in `f_spsram_large` address holding.
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`:
    - Added `[spsram-hold-breach]` monitor for `addr_holding` changes under
      `mem_cen=1` during early post-init cycles.
- Results/next steps:
  - `f_spsram_large` guard is preserved in the wolf emit (`addr_holding` updates
    under `!mem_cen`), so the addr-holding hypothesis is de-prioritized.
  - Convergence point: IFU/BIU request gating path in
    `tests/data/openc910/smart_run/logical/common/cpu_sub_system_axi.v`
    (signals `ifu_biu_rd_req*`, `biu_arvalid_int`, `biu_arvalid_gate`).

## 2026-02-07 11:30
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_112347.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_112347.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - wolf: `* Error: There is no instructions retired in the last 50000 cycles! *`
  - wolf last IFU/BIU toggle: `cycle=2492` (`[c910-ifu-toggle] ... biu_arvalid_* 1->0`)
  - ref IFU/BIU toggles continue through `cycle=348337`
- Hypotheses:
  - Wolf run loses IFU/BIU request activity shortly after reset/early fetch
    (requests stop by ~2.5k cycles), implying a gating/handshake path inside
    `x_ct_biu_top` is deasserting permanently in the wolf emit.
- Instrumentation changes:
  - `tests/data/openc910/smart_run/logical/common/cpu_sub_system_axi.v`:
    - Added `[c910-ifu-toggle]` change detector for
      `ifu_biu_rd_req`, `ifu_biu_rd_req_gate`, `biu_arvalid_int`,
      `biu_arvalid_gate` via hierarchical taps under `x_ct_biu_top`.
- Results/next steps:
  - Convergence point: `x_ct_biu_top` request/gate generation in the wolf emit
    (need to probe upstream gating inputs/qualifiers feeding `arvalid` /
    `arvalid_gate`).

## 2026-02-07 11:38
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_113808.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_113808.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - wolf: `[c910-biu-top]` last toggle at `cycle=2485`
    (`arvalid/arvalid_gate/ifu_biu_rd_req*` drop and never return)
  - wolf: no `lsu_biu_ar_req*` toggles observed in the entire run
  - ref: `lsu_biu_ar_req` toggles begin at `cycle=4045` and continue through
    `cycle=19989`
- Hypotheses:
  - Wolf suppresses LSU BIU read request generation (or gate) before it reaches
    `x_ct_biu_top`, causing the BIU request arbiter to only see IFU traffic.
- Instrumentation changes:
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/biu/rtl/ct_biu_top.v`:
    - Added `[c910-biu-top]` toggle detector for `ifu_biu_rd_req`,
      `ifu_biu_rd_req_gate`, `lsu_biu_ar_req`, `lsu_biu_ar_req_gate`,
      `arvalid`, `arvalid_gate` (first 20k cycles).
- Results/next steps:
  - Convergence point: `x_ct_lsu_top` (or its BIU request generation path)
    because `lsu_biu_ar_req*` never toggles in wolf while ref does.

## 2026-02-07 11:53
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_115321.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_115321.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - wolf: no `[c910-lsu-top]` toggles at all (no activity on
    `pfu_biu_ar_req`, `rb_biu_ar_req`, `wmb_biu_ar_req`,
    `lsu_biu_ar_req`, or their gateclk/grant signals)
  - ref: `rb_biu_ar_req` and `lsu_biu_ar_req` toggles begin at
    `cycle=4045`; `pfu_biu_ar_req` toggles start around `cycle=11893`
- Hypotheses:
  - Wolf fails to generate any LSU BIU read requests; LSU submodules
    (`x_ct_lsu_rb`, `x_ct_lsu_pfu`, `x_ct_lsu_wmb`) appear inert, implying
    the issue is upstream of the LSU bus arbiter (or LSU never receives
    valid issue/memory ops).
- Instrumentation changes:
  - Removed probes:
    - `tests/data/openc910/smart_run/logical/mem/f_spsram_large.v`
      (`[spsram-hold-breach]`)
    - `tests/data/openc910/smart_run/logical/common/cpu_sub_system_axi.v`
      (`[c910-ifu-toggle]`)
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/biu/rtl/ct_biu_top.v`
      (`[c910-biu-top]`)
  - Added probes:
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_top.v`
      (`[c910-lsu-top]` for `pfu/rb/wmb` request + gateclk + grant and
      `lsu_biu_ar_req*`)
- Results/next steps:
  - Convergence point: `x_ct_lsu_top` input side (request generators)
    or the IDU/LSU interface that should drive LSU pipeline ops.

## 2026-02-07 12:05
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_120549.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_120549.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - ref: `[c910-lsu-rb]` toggles start at `cycle=4045` and continue
  - ref: `[c910-lsu-pfu]` toggles begin at `cycle=11893`
  - ref: `[c910-lsu-wmb]` toggles begin at `cycle=4044`
  - wolf: no `[c910-lsu-rb]`, `[c910-lsu-pfu]`, or `[c910-lsu-wmb]` output
    in the entire run
- Hypotheses:
  - Wolf never drives LSU pipeline create signals (RB/PFU/WMB stay idle),
    pointing upstream of LSU request generators (likely IDU→LSU issue).
- Instrumentation changes:
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_rb.v`:
    - Added `[c910-lsu-rb]` toggle detector for `ld_da_rb_create_vld`,
      `rb_biu_ar_req`, `rb_biu_ar_req_gateclk_en`, `bus_arb_rb_ar_grnt`.
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_pfu.v`:
    - Added `[c910-lsu-pfu]` toggle detector for `pfu_lfb_create_vld`,
      `pfu_biu_ar_req`, `pfu_biu_ar_req_gateclk_en`,
      `bus_arb_pfu_ar_grnt`.
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb.v`:
    - Added `[c910-lsu-wmb]` toggle detector for `wmb_ce_create_vld`,
      `wmb_biu_ar_req`, `wmb_biu_ar_req_gateclk_en`,
      `bus_arb_wmb_ar_grnt`.
- Results/next steps:
  - Convergence point: IDU→LSU dispatch path (no RB/PFU/WMB creation in
    wolf). Next probe should target IDU RF pipe3/4 signals feeding LSU.

## 2026-02-07 12:25
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_122512.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_122512.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - ref: `[c910-idu]` and `[c910-core-idu]` pipe3/pipe4 toggles start at
    `cycle=4040` (pipe4) and `cycle=4149` (pipe3)
  - wolf: no `[c910-idu]` or `[c910-core-idu]` output in the entire run
  - wolf: still no `[c910-lsu-rb/pfu/wmb]` output
- Hypotheses:
  - Wolf never asserts IDU→LSU issue signals (pipe3/pipe4), so LSU stays idle.
    The fault is upstream of `ct_idu_top` (decode/dispatch inputs or gating).
- Instrumentation changes:
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/cpu/rtl/ct_core.v`:
    - Added `[c910-core-idu]` toggle detector for
      `idu_lsu_rf_pipe3_sel`, `idu_lsu_rf_pipe4_sel`,
      `idu_lsu_rf_pipe3_inst_ldr`, `idu_lsu_rf_pipe4_inst_str`,
      `idu_lsu_rf_pipe3_inst_type`, `idu_lsu_rf_pipe4_inst_type`.
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_top.v`:
    - Added `[c910-idu]` toggle detector for same pipe3/pipe4 signals.
- Results/next steps:
  - Convergence point: IDU decode/dispatch front-end driving `ct_idu_top`
    (pipe3/pipe4 never assert in wolf). Next probes should target IDU
    input-side signals (e.g., issue queue/dispatch selects).

## 2026-02-07 12:37
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_123706.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_123706.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls)
- Evidence:
  - ref: `[c910-lsiq]` and `[c910-rf-ctrl]` toggles start at
    `cycle=4040+` and continue (issue/bypass/create activity present)
  - wolf: no `[c910-lsiq]`, `[c910-rf-ctrl]`, `[c910-idu]`,
    or `[c910-core-idu]` output in the entire run
- Hypotheses:
  - IDU never dispatches load/store ops into LSIQ in wolf; the fault is
    upstream of `ct_idu_is_lsiq` (likely IDU decode/dispatch inputs).
- Instrumentation changes:
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_is_lsiq.v`:
    - Added `[c910-lsiq]` toggle detector for
      `lsiq_xx_pipe3_issue_en`, `lsiq_xx_pipe4_issue_en`,
      `lsiq_pipe3_bypass_en`, `lsiq_pipe4_bypass_en`,
      `|lsiq_entry_vld|`, `|lsiq_entry_ready|`,
      `dp_lsiq_create0_load`, `dp_lsiq_create0_store`.
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_rf_ctrl.v`:
    - Added `[c910-rf-ctrl]` toggle detector for
      `lsiq_xx_pipe3_issue_en`, `lsiq_xx_pipe4_issue_en`,
      `ctrl_rf_pipe3_inst_vld`, `ctrl_rf_pipe4_inst_vld`,
      `ctrl_rf_pipe3_pipedown_vld`, `ctrl_rf_pipe4_pipedown_vld`,
      `ctrl_rf_pipe3_lch_fail`, `ctrl_rf_pipe4_lch_fail`.
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/cpu/rtl/ct_core.v`:
    - Debug reset uses `idu_rst_b` (avoids undefined `cpurst_b`).
- Results/next steps:
  - Convergence point: IDU dispatch path feeding LSIQ create signals
    (`dp_lsiq_create0_*` never toggle in wolf). Next probe should target
    IDU decode/dispatch modules that drive LSIQ create inputs.

## 2026-02-07 12:51
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_125107.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_125107.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref: `[c910-id-dp] inst*_lsu` toggles start at `cycle=4036` and continue;
    `[c910-is-dp] lsiq_dp_en*` and `create0/1_{load,store}` toggle shortly after
    (e.g., `cycle=4038`).
  - wolf: `[c910-id-dp]` inst*_vld toggles occur early (`cycle=2079`), but there
    are no `inst*_lsu` lines anywhere in the log.
  - wolf: no `[c910-is-dp] lsiq_dp_en*` or `create0/1_{load,store}` toggles in
    the entire run.
- Hypotheses:
  - IDU decode (`ct_idu_id_decd`) is not classifying load/store, so
    `id_inst*_inst_type` never equals LSU/LSU_P5 in wolf.
  - Alternatively, `ct_idu_id_dp` may be extracting the wrong opcode bits into
    `id_inst*_inst`, causing the decoder to miss LSU opcodes.
- Instrumentation changes:
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_is_dp.v`:
    - Added `[c910-is-dp]` toggle detector for
      `ctrl_dp_is_dis_lsiq_create0_sel`, `ctrl_dp_is_dis_lsiq_create1_sel`,
      `ctrl_lsiq_create0_dp_en`, `ctrl_lsiq_create1_dp_en`,
      `dp_lsiq_create0_load`, `dp_lsiq_create0_store`,
      `dp_lsiq_create1_load`, `dp_lsiq_create1_store`.
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_dp.v`:
    - Added `[c910-id-dp]` toggle detector for
      `ctrl_dp_id_inst{0,1,2}_vld` and LSU classification of
      `id_inst{0,1,2}_inst_type` (logs inst word/type when LSU toggles).
- Results/next steps:
  - Convergence point: IDU decode inst_type classification feeding LSIQ
    dispatch. Smallest suspected module is `ct_idu_id_decd` (or the opcode
    extraction feeding it in `ct_idu_id_dp`). Next probe should dump
    `id_inst*_inst` and `id_inst*_inst_type` inside `ct_idu_id_decd` to confirm
    whether LSU decode is missing or the input opcode bits are wrong in wolf.

## 2026-02-07 13:59
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_135943.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_135943.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref: `[c910-id-dp] inst* op=...` continues through cycle 20000, and LSU
    types (`type=0x010/0x030`) appear after ~cycle 4036.
  - wolf: `[c910-id-dp] inst* op=...` stops at `cycle=2483`, and no LSU types
    appear anywhere in the run.
  - wolf: window2 shows PC stuck at 0x4c with `ifu_no_inst=1`/`ifu_fe_stall=1`
    while IDU no longer receives new insts.
- Hypotheses:
  - IFU/IB no longer feeds IDU after early boot in wolf (likely in
    `ct_ifu_ibdp` or upstream buffer select/gating), so IDU never sees later
    LSU instructions.
- Instrumentation changes:
  - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_dp.v`:
    - Added `[c910-id-dp]` log on inst*_vld rising to print opcode, inst_type,
      illegal, and length for inst0/1/2.
- Results/next steps:
  - Convergence point moved upstream of IDU decode: IFU instruction buffer
    feed into IDU. Next probe should target `ct_ifu_ibdp` to log
    `ifu_idu_ib_inst*_vld` and opcode when valid, and track why valid drops.

## 2026-02-07 14:50
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_144048.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_144048.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref: `VCUNT_SIM: CoreMark has been run 2 times...` / `CoreMark Size : 666`
  - wolf: `* Error: There is no instructions retired in the last 50000 cycles! *`
  - wolf IFCTRL shows a refill stall:
    - cycle 2458: `if_self_stall 0->1` with `refill_on=1` and `trans_cmplt=0`
    - cycle 2484: `inst_data_vld 1->0` and `if_self_stall 0->1` again with
      `refill_on=1`/`trans_cmplt=0`, followed by `pcgen_stall 0->1`
  - wolf PCGEN shows the PC bouncing (0x20->0x28->0x30) before the stall at 2484,
    after which no further IFCTRL activity appears in the log.
- Hypotheses:
  - IFU L1 refill path in wolf never reaches `trans_cmplt` after a refill-on
    condition, stalling `if_inst_data_vld` and freezing the frontend.
  - Likely suspect: `ct_ifu_l1_refill` (refill state machine/data return) or
    its handshake with BIU/icache (`ct_ifu_icache_if`).
- Instrumentation changes:
  - Removed probes:
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_dp.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ibdp.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ibctrl.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ipctrl.v`
  - Added probes:
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ifctrl.v`
      (`[c910-ifctrl]` for if_vld/inst_data_vld/if_self_stall + refill signals)
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_pcgen.v`
      (`[c910-pcgen]` for PC changes and ifctrl_stall)
- Results/next steps:
  - Probe `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_l1_refill.v`
    to log refill state, `refill_on`, `trans_cmplt`, and BIU/data return handshakes.
  - If needed, add a small probe in `ct_ifu_icache_if.v` for refill data valid
    and tag/data hit signals to see where `trans_cmplt` is blocked.

## 2026-02-07 15:10
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_144935.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_144935.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - wolf `ct_ifu_l1_refill` shows REQ->WFD1 at cycle 2485 with `refill_on=1`,
    but no `data_vld`/`trans_cmplt` after 2485 (refill FSM stays in WFD1).
  - ref shows each REQ->WFD* transition followed by `data_vld` and progression
    through WFD2/3/4 or INV_WFD states.
- Hypotheses:
  - BIU read data is not returning to IFU refill in wolf after the 2484/2485
    request, or the data-valid path is being gated off before L1 refill.
  - Suspect upstream of L1 refill: IPB data-valid generation or BIU read channel.
- Instrumentation changes:
  - Added `[c910-l1-refill]` toggle detector in
    `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_l1_refill.v`
    (state, req/grant, data_vld/trans_err, refill_on/trans_cmplt).
- Results/next steps:
  - Probe `ct_ifu_ipb.v` to log `biu_ref_data_vld`, `biu_ifu_rd_data_vld`,
    and `ipb_l1_refill_data_vld` around the last request/grant.

## 2026-02-07 15:40
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_145715.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_145715.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - wolf `ct_ifu_ipb` shows `ref_req/ref_grnt` at cycle 2484/2485 but no
    subsequent `biu_data_vld`/`ref_data_vld`/`l1_refill_data_vld` pulses;
    the last `biu_data_vld` is at cycle 2480.
  - ref shows `ref_req/ref_grnt` followed shortly by `biu_data_vld` and
    `l1_refill_data_vld` for each refill request.
- Hypotheses:
  - The final IFU refill request is granted but BIU read data never returns in
    wolf; the failure is upstream of IPB, likely in BIU read-data path or
    pad_biu_rvalid handling.
  - Candidate smallest module: `ct_biu_read_channel` (cur_rdata_buf/rid/valid
    gating to `biu_ifu_rd_data_vld`).
- Instrumentation changes:
  - Added `[c910-ipb]` toggle detector in
    `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ipb.v` to log
    `ref_req/ref_grnt`, `biu_ifu_rd_data_vld`, `biu_ref_data_vld`, and
    `ipb_l1_refill_data_vld`.
- Results/next steps:
  - Probe `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/biu/rtl/ct_biu_read_channel.v`
    for `pad_biu_rvalid`, `cur_rdata_buf_rvalid`, `cur_rdata_buf_rid`,
    `cur_rdata_is_ifu`, and `rack_full` to see why IFU data valid disappears.

## 2026-02-07 15:56
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_155605.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_155605.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - wolf `ct_biu_read_channel` shows `pad_arvalid` for 0x50 at cycle 2485,
    but no subsequent `pad_rvalid` after the 2479/2480 return.
  - `axi_interconnect128` window (2480–2520) shows `arvalid=0`/`rvalid=0`
    after the 0x40 transaction; no AR for 0x50 reaches the interconnect.
- Hypotheses:
  - The final IFU AR request is lost between BIU output and AXI interconnect
    (candidate: `axi_fifo` or top-level wiring into `soc.v`).
- Instrumentation changes:
  - Removed probes in IFU/LSU paths:
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ifctrl.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_pcgen.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_l1_refill.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ipb.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_rb.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_pfu.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb.v`
    - `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_top.v`
  - Added interconnect read-path debug:
    - `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v`
      (`[c910-axi-ic]` rvalid/rready/rsel + 2480–2520 window)
- Results/next steps:
  - Add probes in `tests/data/openc910/smart_run/logical/axi/axi_fifo.v` to
    see if `biu_pad_arvalid/araddr` reaches the FIFO, and whether `pop_req`
   /`fifo_pad_arvalid` assert for the 0x50 request.

## 2026-02-07 16:04
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_160432.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_160432.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref `axi_fifo` sees `biu_arvalid` for addr 0x50 at cycle 2504/2505
    (`create_vld=1`, `fifo_pad_arvalid=1`).
  - wolf `axi_fifo` never logs `biu_araddr=0x50`; window 2480–2520 shows
    `biu_arvalid=0`, `create_en=0`, `fifo_pad_arvalid=0`.
- Hypotheses:
  - `biu_pad_arvalid/araddr` is being lost before `axi_fifo` in wolf.
- Instrumentation changes:
  - Added `[c910-axi-fifo]` in
    `tests/data/openc910/smart_run/logical/axi/axi_fifo.v`
    (create/pop/fifo_arvalid + 2480–2520 window).
- Results/next steps:
  - Probe BIU top output to confirm whether `biu_pad_arvalid` still asserts
    for 0x50 in wolf.

## 2026-02-07 16:13
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_161339.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_161339.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - wolf `ct_biu_top` shows `biu_pad_arvalid` 0->1 for addr 0x50 at cycle 2485.
  - wolf `axi_fifo` still shows no `biu_araddr=0x50`.
- Hypotheses:
  - Loss happens between `ct_biu_top` and `axi_fifo` (wrapper wiring).
- Instrumentation changes:
  - Added `[c910-biu-top]` in
    `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/biu/rtl/ct_biu_top.v`
    (arvalid/araddr/arready + 2480–2520 window).
- Results/next steps:
  - Probe `cpu_sub_system_axi` (wrapper output) to see whether 0x50 survives
    the wrapper boundary.

## 2026-02-07 16:21
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_162109.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_162109.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref `cpu_sub_system_axi` shows `biu_pad_arvalid` for addr 0x50 at cycle 2504.
  - wolf `cpu_sub_system_axi` never shows `araddr=0x50` (last is 0x40 at 2478),
    even though `ct_biu_top` still shows 0x50 at 2485.
- Hypotheses:
  - The 0x50 AR is dropped between `ct_biu_top` output and the wrapper
    output in `cpu_sub_system_axi`/`rv_integration_platform` (likely port
    wiring or missing connection in the wrapper/ct_core boundary).
- Instrumentation changes:
  - Added `[c910-cpu-axi]` in
    `tests/data/openc910/smart_run/logical/common/cpu_sub_system_axi.v`
    (biu_pad_arvalid/araddr/arready + 2480–2520 window).
- Results/next steps:
  - Next probe should be inside `rv_integration_platform.v` (and/or `ct_core`
    top-level wrapper) to see if `biu_pad_arvalid` is lost between core and
    wrapper outputs.

## 2026-02-07 20:35
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_202754.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_202754.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref `[c910-open]`/`[c910-rvip]` show `arvalid 0->1` with `araddr=0x50` at cycle 2504.
  - wolf `[c910-open]`/`[c910-rvip]` show only `araddr=0x40` across cycles 2480-2520 (no 0x50), while
    wolf `[c910-biu-top]` still shows `arvalid 0->1` with `araddr=0x50` at cycle 2485.
- Hypotheses:
  - The 0x50 AR is dropped between `ct_biu_top` and the top-level `biu_pad_*` outputs inside
    `ct_ciu_top` (or its ebiu/bus-io submodules), likely a missing/incorrect connection in
    the wolf emit.
- Instrumentation changes:
  - Added `[c910-open]` in `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/cpu/rtl/openC910.v`.
  - Added `[c910-rvip]` in `tests/data/openc910/smart_run/logical/common/rv_integration_platform.v`.
  - Removed `[c910-axi-ic]` and `[c910-axi-fifo]` probes from
    `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v` and
    `tests/data/openc910/smart_run/logical/axi/axi_fifo.v`.
- Results/next steps:
  - Convergence point: `ct_ciu_top` output path (`biu_pad_arvalid/araddr`) between
    `ct_biu_top` and `openC910` outputs.
  - Next probe should be inside `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_top.v`
    (and possibly `ct_ciu_ebiuif.v` or `ct_ciu_bus_io.v`) to log the internal AR signals feeding
    `biu_pad_*` and pinpoint where 0x50 is dropped.

## 2026-02-07 21:14
- Command: `make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260207_210755.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260207_210755.log
- Equivalence: Not equivalent (ref completes CoreMark; wolf stalls at 100k cycles)
- Evidence:
  - ref `[c910-ebiuif]` shows `snb1_v 0->1` with `snb1_a=0x50` and `arvalid 0->1` at cycle 2500;
    wolf shows no `snb1_v` assertion in the same window (all zero).
  - ref `[c910-snb]` (snb1 instance) shows `arvalid 0->1 araddr=0x50` at cycle 2500;
    wolf `[c910-snb]` never asserts `arvalid` in the 2480-2520 window (stays 0, addr=0x40).
- Hypotheses:
  - The missing 0x50 AR is now isolated to the snb1 path inside `ct_ciu_snb` (or its arb/SAB inputs);
    `snb1_ebiuif_arvalid` never asserts in wolf.
- Instrumentation changes:
  - Added `[c910-ebiuif]` in `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ebiuif.v`
    (snb0/1 arvalid/araddr, rd_req, depd, sel, ebiuif arvalid/araddr).
  - Added `[c910-snb]` in `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb.v`
    (snb_ebiuif_arvalid/araddr + piu*_snb_ar_req + bmbif_snb_bar_req, snb1 tagged).
- Results/next steps:
  - Convergence point: `ct_ciu_snb` snb1 output (`snb_ebiuif_arvalid`) or its immediate arb/SAB
    sources that generate `sab_arb_ebiu_ar_req` / `sab_arb_ebiu_ar_bus`.
  - Next probe should target `ct_ciu_snb_arb.v` (or add `sab_arb_ebiu_ar_req`/bus logging in
    `ct_ciu_snb.v`) to see whether the request is missing before arbitration or dropped by it.

## 2026-02-08 00:52
- Command: `C910_SIM_MAX_CYCLE=5000 C910_WAVEFORM=1 make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.log
- FSTs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.fst
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.fst
- Equivalence: Not equivalent (both hit the 5000-cycle limit; wolf stops retire printing after idx=19)
- Evidence:
  - Log diff: ref prints `[c910-iret]` idx 20..100 (cycle 2523..4350), wolf prints only idx 1..19.
  - Waveform ROI: ref shows `TOP.retire0` pulses starting at time 252645, wolf stays 0 from that point.
- Waveform ROI details (jsonl fill + include-initial):
  - Top-level retire signals: `TOP.retire0/1/2`, `TOP.core0_retire{0,1,2}_pc`, `TOP.core0_cpu_no_retire` with `--t0 252500 --t1 253500`.
  - RTU/IDU chain: `x_ct_rtu_top.rtu_pad_retire0`, `x_ct_rtu_top.rtu_yy_xx_retire0`,
    `x_ct_rtu_top.rtu_idu_retire0_inst_vld`, `x_ct_rtu_retire.rtu_idu_retire0_inst_vld`,
    `x_ct_idu_top.rtu_idu_retire0_inst_vld`, `x_ct_idu_rf_prf_eregfile.rtu_idu_retire0_inst_vld`
    with `--t0 252600 --t1 252800`.
- Results/next steps:
  - Convergence point: `x_ct_idu_rf_prf_eregfile` (core0) — `rtu_idu_retire0_inst_vld` stops toggling in wolf at time 252645 while ref continues; downstream RTU/core/top-level retire signals follow this divergence.
  - Next: probe inputs to `x_ct_idu_rf_prf_eregfile` around time 252640–252700 (writeback valid/enables, ROB/commit interface, retire gating) to see what first differs in wolf vs ref.

## 2026-02-08 01:40
- Command: `python3 tools/fst_roi/fst_roi.py ...` (ROI-only backtrace, no rerun)
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.log
- FSTs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.fst
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.fst
- Equivalence: Not equivalent
- Evidence:
  - `x_ct_idu_rf_prf_eregfile` inputs: `cp0_idu_icg_en`, `cp0_yy_clk_en`, `cpurst_b`, `pad_yy_icg_scan_en`, `rtu_idu_pst_ereg_retired_released_wb` match; only `rtu_idu_retire0_inst_vld` pulses in ref (e.g., time 252645/252649/252697), missing in wolf.
  - RTU/ROB chain: `rob_read0_inst_vld`/`rob_read0_cmplted`/`rob_retire_inst0_vld` pulse in ref around 252641–252649, missing in wolf.
  - IDU chain: `ctrl_id_pipedown_inst0_vld` → `ir_pipedown_inst0_vld` → `is_dis_inst0_vld` → `idu_rtu_rob_create0_en` pulse in ref around 252633–252639, missing in wolf.
  - IFU IBCTRL/IBDP: `ibctrl_ibdp_bypass_inst_vld` (252631) and `ibctrl_ibdp_ibuf_inst_vld` (252633) pulse in ref, missing in wolf; `ipctrl_ibctrl_vld` also pulses in ref at 252631 but stays 0 in wolf.
  - IFU IPCTRL: `ifctrl_ipctrl_vld`/`ip_data_vld` pulse in ref at 252629; wolf stays 0. `ip_self_stall=0` in ref vs `ip_self_stall=1` in wolf.
  - IFU IFCTRL: `if_inst_data_vld=1` in ref vs `0` in wolf; wolf holds `l1_refill_ifctrl_refill_on=1` and `l1_refill_ifctrl_trans_cmplt=0`.
  - L1 refill: `ipb_l1_refill_data_vld` pulses in ref at 252627; wolf never asserts it. `refill_cur_state` stays `0100` with `refill_sm_on=1` in wolf.
  - IPB: `biu_ref_data_vld` (`biu_ifu_rd_data_vld`) pulses in ref at 252627; wolf stays 0.
  - BIU read channel: `pad_biu_rvalid` pulse in ref at 252625 (and `biu_ifu_rd_data_vld` at 252627); wolf never asserts `pad_biu_rvalid`.
- Waveform ROI details (jsonl fill + include-initial):
  - IDU/RTU/ROB: `x_ct_idu_rf_prf_eregfile.*`, `x_ct_rtu_top.*` with `--t0 252600 --t1 252700`.
  - IFU IBCTRL/IBDP/IPCTRL/IFCTRL: key `*_vld`/stall signals with `--t0 252628 --t1 252638`.
  - L1 refill/IPB/BIU read channel: `refill_sm_on`, `refill_cur_state`, `ipb_l1_refill_data_vld`, `biu_ifu_rd_data_vld`, `pad_biu_rvalid` with `--t0 252620 --t1 252660`.
- Results/next steps:
  - Convergence point: `x_ct_biu_top.x_ct_biu_read_channel.pad_biu_rvalid` (ref pulse at time 252625; wolf missing). This is the earliest divergence found in the ROI chain.
  - Next: trace the read-response path outside BIU (e.g., `x_soc.x_axi2ahb.pad_biu_rvalid`) and confirm whether the corresponding AR request is issued/accepted in wolf; if AR is missing, backtrace `biu_pad_arvalid/araddr` to the IFU request source.

## 2026-02-08 07:55
- Command: `python3 tools/fst_roi/fst_roi.py ...` (ROI-only backtrace, no rerun)
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.log
- FSTs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.fst
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.fst
- Equivalence: Not equivalent
- Evidence:
  - Response path: `x_ct_piu0_top.snb1_piu_rvalid` pulses in ref at time 252621 but is missing in wolf; it feeds `ciu_ibiu_rvalid` (pad_ibiu0_rvalid) at time 252625.
  - Inside SNB1: `x_ct_ciu_snb_1.ebiuif_snb_rvalid` pulses in ref at 252617; wolf stays 0. This directly drives `snb_piu0_rvalid`.
  - EBIU interface: `x_ct_ciu_ebiuif.ebiu_ebiuif_snb1_rvalid` pulses in ref at 252617; wolf missing. That input is sourced by `x_ct_ebiu_top.ebiu_ebiuif_snb1_rvalid`.
  - EBIU top input: `x_ct_ebiu_top.pad_ebiu_rvalid` is 1 in ref around 252615 but remains 0 in wolf, so the read-response never enters the CIU/EBIU path in wolf.
  - Top-level chain: `pad_biu_rvalid` pulses in ref at time 252615 across TOP/sim_top/x_soc/cpu_sub_system_axi/rv_integration_platform/x_cpu_top/x_ct_ciu_top, but never asserts in wolf.
  - Request context: `ibiu0_pad_arvalid` pulses at 252585/252637/252689 in ref; wolf only has the 252585 pulse (missing later requests). Even with the shared 252585 request, the 252615 response is missing in wolf.
- Waveform ROI details (jsonl fill + include-initial):
  - PIU/SNB chain: `x_ct_piu0_top.{snb1_piu_rvalid,ciu_ibiu_rvalid}` with `--t0 252620 --t1 252640`.
  - SNB1 internals: `x_ct_ciu_snb_1.{ebiuif_snb_rvalid,snb_piu0_rvalid}` with `--t0 252616 --t1 252636`.
  - EBIU interface/top: `x_ct_ciu_ebiuif.ebiu_ebiuif_snb1_rvalid`, `x_ct_ebiu_top.pad_ebiu_rvalid` with `--t0 252616 --t1 252636`.
  - Top-level pad response: `pad_biu_rvalid` chain with `--t0 252612 --t1 252620`.
- Results/next steps:
  - Convergence point: `x_ct_ebiu_top.pad_ebiu_rvalid` / top-level `pad_biu_rvalid` — ref asserts at time 252615, wolf remains 0. This is the earliest missing read-response signal feeding SNB1/PIU/IBIU0 and ultimately the IFU refill.
  - Next: check why `pad_biu_rvalid` is not produced in wolf for the 252585 request (e.g., verify `biu_pad_arvalid`/`ibiu0_pad_arvalid` reach the bus adapter and whether `x_axi2ahb` or the memory model returns `pad_biu_rvalid` in wolf).

## 2026-02-08 08:12
- Command: `python3 tools/fst_roi/fst_roi.py ...` (ROI-only backtrace, no rerun)
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.log
- FSTs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.fst
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.fst
- Equivalence: Not equivalent
- Evidence:
  - SoC read channel: ref asserts `fifo_pad_arvalid`/`arvalid_s0` at 252611 with addr 0x50 and `rvalid_s0` at 252615; wolf has no `fifo_pad_arvalid`/`arvalid_s0` pulse in the window (only the earlier 252563 return).
  - `x_axi_fifo` shows the missing AR: ref has `biu_pad_arvalid=1` and `create_en/create_vld=1` at 252609 (addr 0x50), then `fifo_pad_arvalid` at 252611; wolf shows no `biu_pad_arvalid` or create/pop activity in 252560–252630.
  - Core output vs CIU output: `x_ct_top_0.x_ct_biu_top.biu_pad_arvalid` pulses at 252585 with addr 0x50 in both ref/wolf, but `x_ct_ciu_top.biu_pad_arvalid` at 252609 appears only in ref (wolf never asserts), so the drop happens inside CIU/SNB path.
  - `ct_piu0_top` inputs match: `ibiu_ciu_arvalid` (252585) and `piu_snb1_ar_req` (252587) pulse in both ref/wolf.
  - `ct_ciu_snb_1` diverges after PIU: `piu0_snb_ar_req` matches, but `sab_arb_ebiu_ar_req`/`snb_ebiuif_arvalid` pulse at 252605/252607 only in ref.
  - `ct_ciu_snb_1` SAB inputs match but output missing: `sab_ar_create_en`/`sab_ar_create_bus` match in ref/wolf at 252589, yet `sab_ebiur_req_vld` and `sab_ebiu_rd_sel` assert only in ref at 252605.
  - `x_ct_ciu_snb_sab_entry0` state machine diverges: ref asserts `memr_req_vld` at 252603 and `memr_cur_state=MEMR_REQ` at 252605; wolf never asserts `memr_req_vld` and stays in `main_cur_state=POP`. Ref shows `sab_rack_sel` bit0 at 252579 and `pop_en=1` at 252581, while wolf never asserts `sab_rack_sel` or `pop_en`.
- Hypotheses:
  - The earliest mismatch is inside `x_ct_ciu_snb_1.x_ct_ciu_snb_sab_entry0`: inputs (`sab_ar_create_en`/bus) match, but the entry never exits POP or raises `memr_req_vld`, preventing `sab_ebiur_req_vld` and downstream EBIU AR.
  - Missing `sab_rack_sel` in wolf suggests the rack/back path into `ct_ciu_snb_sab_entry0` is not firing, so the entry cannot pop and accept the next request.
- Waveform ROI details (jsonl event + include-initial):
  - SoC AR/R channel + AXI FIFO: `TOP.sim_top.{fifo_pad_arvalid,fifo_pad_araddr,arvalid_s0,rvalid_s0,rdata_s0_lo/hi}`, `x_axi_fifo.{biu_pad_arvalid,create_en,create_vld,pop_req,pop_en,fifo_pad_arvalid}` with `--t0 252560 --t1 252630`.
  - CIU/PIU/SNB chain: `x_ct_piu0_top.{ibiu_ciu_arvalid,piu_snb1_ar_req}`, `x_ct_ciu_snb_1.{piu0_snb_ar_req,sab_arb_ebiu_ar_req,snb_ebiuif_arvalid}` with `--t0 252560 --t1 252630`.
  - SAB + entry0: `x_ct_ciu_snb_1.{sab_ar_create_en,sab_ar_create_bus,sab_ebiur_req_vld,sab_ebiu_rd_sel,sab_rack_sel}`, `x_ct_ciu_snb_sab_entry0.{main_cur_state,memr_cur_state,memr_req_vld,pop_en}` with `--t0 252560 --t1 252630`.
- Results/next steps:
  - Convergence point: `x_ct_ciu_snb_1.x_ct_ciu_snb_sab_entry0` (entry stuck in POP; `memr_req_vld` never asserts in wolf despite matching create bus/en).
  - Next: trace why `sab_rack_sel` is missing in wolf (check `sab_memr_vld/sel` and rack/back inputs into `ct_ciu_snb_sab_entry0`, or the rresp path into SAB) to see whether the rack path is dropped by emit or gated off upstream.

## 2026-02-08 08:56
- Command: `python3 tools/fst_roi/fst_roi.py ...` (ROI-only backtrace, no rerun)
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.log
- FSTs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_003353.fst
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_003353.fst
- Equivalence: Not equivalent
- Evidence:
  - `ct_ciu_snb_1` rack source: ref asserts `piu0_snb_rack` at 252579 (and `sab_rack_sel` bit0), wolf never asserts `piu0_snb_rack`.
  - `ct_piu0_top` inputs match: `ibiu_ciu_rack` and `ibiu_ciu_rack_reg` pulse at 252577/252579 in both ref/wolf; `rack_dfifo_pop_bus_vld=1` in both.
  - Divergence is in rack DFIFO data: ref `rack_dfifo_pop_bus=0100000` while `rack_dfifo_pop_bus_vld=1`, wolf drops `rack_dfifo_pop_bus` to `0000000` at 252579, clearing bit5 and suppressing `piu_snb1_rack`.
  - Inside `x_ct_piu_rack_dfifo` (ct_fifo):
    - Inputs match: `fifo_create_en`/`fifo_create_en_dp` pulse at 252571 with `fifo_create_data=0100000` in both.
    - `fifo_entry_create_dp` asserts entry0 in both at 252571.
    - Ref updates `fifo_entry_cont[0]=0100000` at 252573; wolf never updates `fifo_entry_cont[0]` (stays 0), despite the same create inputs.
  - Result: `fifo_pop_data`/`rack_dfifo_pop_bus` becomes unstable in wolf, causing `piu_snb1_rack` to remain low and keeping `sab_rack_sel` at 0; `x_ct_ciu_snb_sab_entry0` stays in POP and never raises `memr_req_vld`.
- Hypotheses:
  - The earliest mismatch is inside `ct_fifo` used by `x_ct_piu_rack_dfifo`: entry content (`fifo_entry_cont`) is not being written in wolf even though create signals match. This suggests an emit bug around the gated entry clock or array write (`fifo_entry_cont[i] <= fifo_create_data`) in generated SV.
- Waveform ROI details (jsonl event + include-initial):
  - PIU rack path: `x_ct_piu0_top.{ibiu_ciu_rack,ibiu_ciu_rack_reg,piu_snb1_rack,piu_snbx_rack_sid,rack_dfifo_pop_bus_vld,rack_dfifo_pop_bus}` with `--t0 252560 --t1 252630`.
  - DFIFO internals: `x_ct_piu_rack_dfifo.{fifo_create_en,fifo_create_en_dp,fifo_entry_create_dp,fifo_create_data,fifo_entry_cont[0],fifo_pop_data}` with `--t0 252560 --t1 252630`.
- Results/next steps:
  - Convergence point: `x_ct_piu0_top.x_ct_piu_rack_dfifo` (ct_fifo entry write). Inputs match; `fifo_entry_cont[0]` fails to update only in wolf, leading to missing `piu0_snb_rack`.
  - Next: inspect wolf emit of `ct_fifo` for this instance (array write) to find the concrete SV mismatch.

  - Phase: wolf emit inspection
    - `tests/data/openc910/smart_run/work_wolf/wolf_emit/sim_top_wolf.sv` shows `x_ct_piu_rack_dfifo` uses `ct_fifo__p1247403345692613480`.
    - In that module, each `fifo_entry_cont[i]` is driven by *three* always blocks (reset, create, and a write-back from `__mem_data_i`). Example for entry 7 around lines 134764–134779:
      - `fifo_entry_cont[7] <= __expr_61` on create.
      - `fifo_entry_cont[7] <= __mem_data_0` when not creating.
      - `__mem_data_0 <= __mem_rd_0` on the same clock when not creating.
    - This creates a one-cycle feedback that causes `fifo_entry_cont[7]` to oscillate between old and new values when the clock is free-running, matching the wolf waveform where `fifo_entry_cont[7]` toggles every cycle after the create pulse.
    - The original `ct_fifo.v` uses a *single* always block per entry with an explicit self-hold (`else fifo_entry_cont[i] <= fifo_entry_cont[i]`), which does not oscillate. The wolf emit’s memory-read/write lowering is therefore not behaviorally equivalent here.
    - Likely root cause in wolf-sv-parser: the synchronous memory read lowering in `src/convert.cpp` (creation of `__mem_data_` / `__mem_rd_` + separate write-back) is incorrectly generated for this self-hold pattern.

## 2026-02-08 09:15
- Command: minimal repro with iverilog (`build/artifacts/c910_minrepro/ct_fifo_min.sv`)
- Equivalence: Not equivalent (behavioral mismatch between ref-style and wolf-style ct_fifo)
- Evidence:
  - Ref model holds entry stable after create; wolf model toggles every cycle.
  - Simulation output excerpt:
    - `t=9000  ref=0100000 wolf=0100000`
    - `t=11000 ref=0100000 wolf=0000000`
    - `t=13000 ref=0100000 wolf=0100000`
- Hypothesis:
  - The wolf memory-lowering transforms a single always block with self-hold into multiple always blocks with a read-back register; with a free-running gated clock, this becomes a ping-pong that oscillates the entry contents.
- Results/next steps:
  - Minimal repro confirms the structural issue independent of the full C910 design.
  - Next: implement a fix in the memory-lowering path (`src/convert.cpp`) to avoid the read-back write when the source is the same memory element (self-hold), or to coalesce the split always blocks into a single register update.

## 2026-02-08 11:02
- Command: `C910_SIM_MAX_CYCLE=5000 C910_WAVEFORM=1 make run_c910_diff -j`
- Logs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_104416.log
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_104416.log
- FSTs:
  - ref: build/logs/c910/c910_ref_coremark_20260208_104416.fst
  - wolf: build/logs/c910/c910_wolf_coremark_20260208_104416.fst
- Equivalence: Not equivalent
- Evidence:
  - CoreMark summary lines are absent in both logs, and the `c910-iret` trace diverges after idx 38 (ref shows PCs around 0x1850/0x186x; wolf switches to PCs around 0x110/0x140).
  - Waveform top-level BIU AR diverges: `TOP.sim_top.x_soc.biu_pad_araddr/arvalid` mismatch begins at t=255511 (wolf issues 0x80; ref issues 0x100).
  - Read channel mismatch appears earlier: `x_ct_biu_read_channel` shows `araddr/arvalid` on 0xA0 at t=255471 in wolf, while ref issues 0xC0 at t=255475.
  - BIU request arbiter confirms source: `x_ct_biu_req_arbiter.ifu_biu_rd_req=1` with addr 0xA0 at t=255471 in wolf vs 0xC0 at t=255475 in ref; LSU AR req stays 0 in both.
  - IFU IPB address generation diverges at t=255471: `l1_refill_ipb_vpc` is 0xAA in wolf vs 0xC0 in ref; `ref_addr_inc` is 0x3 in wolf vs 0x4 in ref. Control `pcgen_ipb_chgflw` also differs at t=255469.
  - PCGEN outputs diverge at t=255469: `pcgen_ifctrl_pc/pcgen_ifdp_pc` in wolf are 0x58/0x55 range while ref is 0x5B/0x60 range (binary diffs). `addrgen_pcgen_pc` stays 0 in both, pointing to PCGEN logic as the origin.
- Hypotheses:
  - Root cause is in IFU PC generation (`x_ct_ifu_top.x_ct_ifu_pcgen`): PC/next-PC calculation differs, producing wrong VPC/increment in `x_ct_ifu_ipb`, which then drives a different IFU BIU read address.
  - Likely conversion issue in arithmetic/bit-slicing around `pcgen_ifdp_inc_pc` or `ref_addr_inc` (width/sign/constant handling), since increment becomes 0x3 instead of 0x4.
- Waveform ROI details (jsonl fill):
  - Top-level BIU: `TOP.sim_top.x_soc.biu_pad_araddr/arvalid` plus `x_ct_top_0`/`x_ct_biu_top`/`x_ct_biu_read_channel` AR signals with `--t0 255400 --t1 255700`.
  - Read channel internals: `x_ct_biu_read_channel.{cur_raddr_buf*,read_busy,read_ar_clk_en,ifu_biu_r_ready,lsu_biu_r_linefill_ready}` with `--t0 255450 --t1 255520`.
  - BIU request arbiter: `x_ct_biu_req_arbiter.{ifu_biu_rd_req/addr,lsu_biu_ar_req/addr,araddr/arvalid,biu_ifu_rd_grnt}` with `--t0 255450 --t1 255520`.
  - IFU path: `x_ct_ifu_ipb.{l1_refill_ipb_vpc,ref_addr_inc,pcgen_ipb_chgflw,ifu_biu_rd_addr}` and `x_ct_ifu_top.{pcgen_ifctrl_pc,pcgen_ifdp_pc,pcgen_ifdp_inc_pc}` with `--t0 255450 --t1 255520`.
- Results/next steps:
  - Convergence point: `TOP.sim_top.x_soc.x_cpu_sub_system_axi.x_rv_integration_platform.x_cpu_top.x_ct_top_0.x_ct_core.x_ct_ifu_top.x_ct_ifu_pcgen` (PCGEN outputs diverge first at t=255469; downstream IPB/BIU differences are consequent).
  - Next: inspect wolf-emitted SV for `x_ct_ifu_pcgen` and `x_ct_ifu_ipb` around PC increment/address generation (look for width truncation or constant folding bugs in `pcgen_ifdp_inc_pc`/`ref_addr_inc`).

## 2026-02-08 11:19
- Command: `rg -n "inc_pc|reissue_pcload|ref_addr_inc" tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/{ct_ifu_pcgen.v,ct_ifu_ipb.v}` and `sed -n '450,485p' .../ct_ifu_pcgen.v`, `sed -n '650,690p' .../ct_ifu_ipb.v`
- Logs/FSTs: same as 2026-02-08 11:02 run.
- Equivalence: N/A (RTL vs wolf emit inspection only).
- Evidence:
  - The PC increment logic in RTL (`ct_ifu_pcgen.v`) matches wolf emit exactly: `inc_pc_hi = if_pc[38:3] + {35'b0, !ifctrl_pcgen_reissue_pcload}` and `inc_pc = {inc_pc_hi, {3{ifctrl_pcgen_reissue_pcload}} & if_pc[2:0]}`.
  - IPB refill increment logic in RTL (`ct_ifu_ipb.v`) matches wolf emit: `ref_addr_inc = l1_refill_ipb_ppc[39:6] + 26'b1` and `ipb_icache_if_index_pre = l1_refill_ipb_vpc[PC_WIDTH-1:6] + 34'h1` (wolf emit uses 34'h1).
  - `ifctrl_pcgen_reissue_pcload` in RTL is simply `icache_reissue`; wolf emit preserves this (reset gating is folded into `__expr_102`).
- Hypotheses:
  - Since PCGEN/IPB arithmetic matches RTL, the mismatch likely comes from upstream inputs (e.g., `icache_reissue`, `l1_refill_ipb_ppc/vpc`, or earlier PCGEN control signals) diverging between ref and wolf rather than a local width/truncation error here.
- Results/next steps:
  - Next: compare ref vs wolf waveforms for `icache_reissue`, `ifctrl_pcgen_reissue_pcload`, and `l1_refill_ipb_ppc/vpc` around t=255469 to locate the first upstream signal that diverges before PCGEN outputs.

## 2026-02-08 11:31
- Command:
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_pcgen.{if_pc,pcgen_ifctrl_pc,pcgen_ifdp_pc,ipctrl_pcgen_chgflw_pc}*' --t0 0 --t1 255520 --jsonl-mode fill --include-initial`
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_ifctrl.{ifctrl_pcgen_stall,ifctrl_pcgen_stall_short,if_self_stall,if_stage_stall,ipctrl_ifctrl_stall,ipctrl_ifctrl_stall_short,pcgen_ifctrl_way_pred_stall,rtu_ifu_xx_dbgon,l1_refill_ifctrl_start,l1_refill_ifctrl_idle,ipb_ifctrl_prefetch_idle,if_inst_data_vld,if_pc_vld}*' --t0 255400 --t1 255450 --jsonl-mode fill --include-initial`
- Logs/FSTs: same as 2026-02-08 11:02 run.
- Equivalence: Not equivalent.
- Evidence:
  - First divergence among PCGEN core state/outputs occurs at t=255471: `x_ct_ifu_pcgen.if_pc`, `pcgen_ifctrl_pc`, and `pcgen_ifdp_pc` differ (ref 0xC0 vs wolf 0x55/0x58 range).
  - Inputs feeding PCGEN diverge earlier at t=255425: `x_ct_ifu_ifctrl.ipctrl_ifctrl_stall` and `ipctrl_ifctrl_stall_short` assert in wolf (1) while ref remains 0, causing `ifctrl_pcgen_stall` and `ifctrl_pcgen_stall_short` to assert only in wolf.
- Hypotheses:
  - Root cause is upstream of PCGEN, likely inside `x_ct_ifu_ipctrl` or its inputs, since `ipctrl_ifctrl_stall(_short)` diverges first and directly gates PCGEN progress.
- Results/next steps:
  - Next: compare ref vs wolf waveforms for `x_ct_ifu_ipctrl` inputs that feed `ipctrl_ifctrl_stall(_short)` (e.g., `ifctrl_ipctrl_vld`, `ifctrl_ipctrl_if_pcload`, `ipb_ipctrl_stall`, `ibctrl_ipctrl_stall`, etc.) around t=255420–255430 to locate the earliest upstream mismatch.

## 2026-02-08 12:00
- Command: multiple single-signal ROI extractions (to avoid slow multi-signal scans), e.g.
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_ipctrl.{ip_refill_pre,miss_under_refill_stall,l1_refill_ipctrl_busy,ifdp_ipctrl_way0_28_24_hit,ifdp_ipctrl_way0_23_16_hit,ifdp_ipctrl_way0_15_8_hit,ifdp_ipctrl_way0_7_0_hit}' --t0 255420 --t1 255430 --jsonl-mode fill --include-initial`
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_ifdp.{ifdp_icache_way0_28_24_hit,icache_tag_way0_28_24_hit,icache_tag_way0_23_16_hit,icache_tag_way0_15_8_hit,icache_tag_way0_7_0_hit}' --t0 255420 --t1 255430 --jsonl-mode fill --include-initial`
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_ifctrl.ifctrl_ifdp_pipedown' --t0 255420 --t1 255430 --jsonl-mode fill --include-initial`
- Logs/FSTs: same as 2026-02-08 11:02 run.
- Equivalence: Not equivalent.
- Evidence:
  - At t=255425, `ip_refill_pre=1` and `miss_under_refill_stall=1` in wolf, while ref has `ip_refill_pre=0` and `miss_under_refill_stall=0` (both see `l1_refill_ipctrl_busy=1`).
  - The only differing input to `ip_refill_pre` at t=255425 is `icache_way0_hit`: ref=1, wolf=0. This traces to `ifdp_ipctrl_way0_28_24_hit` (ref=1, wolf=0); other way0 hit slices match.
  - `ifdp_ipctrl_way0_28_24_hit` is a registered copy of `ifdp_icache_way0_28_24_hit` gated by `ifctrl_ifdp_pipedown`. `ifctrl_ifdp_pipedown` drops earlier in wolf (t=255425) than ref (t=255429), so wolf misses the update and keeps the stale 0.
  - The combinational tag-compare `icache_tag_way0_28_24_hit` (valid+tag[27:24]) rises earlier in ref (t=255423) than wolf (t=255425). The other tag segments (23_16/15_8/7_0) are 1 in both, isolating the mismatch to the 28_24 slice (valid bit + upper tag).
- Hypotheses:
  - Root cause has moved upstream into the I-cache tag data/valid path feeding `icache_tag_way0_28_24_hit`. The delayed rise in wolf suggests the way0 tag valid bit (tag[28]) or the upper tag nibble arrives late/incorrect compared to ref.
- Results/next steps:
  - Next: inspect the wolf-emitted SV for the I-cache tag path feeding `icache_if_ifdp_tag_data0[28:24]` (or any explicit valid-bit signal if present) and its update enables/gated clocks. If needed, compare the timing of tag-data capture logic in the corresponding RTL module (likely IFU I-cache or IFDP tag-read path).

## 2026-02-08 12:11
- Command: targeted ROI on I-cache tag-array control and PCGEN interface signals:
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_icache_if.ifu_icache_tag_{clk_en,cen_b}' --t0 255400 --t1 255430 --jsonl-mode fill --include-initial`
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signal '...x_ct_ifu_icache_if.{ifctrl_icache_if_tag_req,ifctrl_icache_if_read_req_tag,l1_refill_icache_if_wr,l1_refill_icache_if_first,l1_refill_icache_if_last,pcgen_icache_if_chgflw,pcgen_icache_if_seq_tag_req,ipb_icache_if_req,pcgen_icache_if_gateclk_en,ipb_icache_if_req_for_gateclk,cp0_ifu_icache_en}' --t0 255420 --t1 255430 --jsonl-mode fill --include-initial`
- Logs/FSTs: same as 2026-02-08 11:02 run.
- Equivalence: Not equivalent.
- Evidence:
  - At t=255425, `pcgen_icache_if_seq_tag_req` and `pcgen_icache_if_gateclk_en` are 1 in ref but 0 in wolf; all other tag-control inputs are identical.
  - Correspondingly, `ifu_icache_tag_cen_b` goes low (read enable) in ref at t=255425, but stays high in wolf; and `ifu_icache_tag_clk_en` drops earlier in wolf at t=255425 (ref stays high until t=255429).
  - This explains the earlier observation that `icache_tag_way0_28_24_hit` rises in ref at t=255423 but is delayed in wolf until t=255425, which then propagates into stale `ifdp_ipctrl_way0_28_24_hit` and the `miss_under_refill_stall` assertion.
- Hypotheses:
  - The divergence is now pinned to PCGEN stall gating: `pcgen_icache_if_seq_tag_req` = `!ifctrl_pcgen_stall && (pc_bus[4:3]==2'b00)` and `pcgen_icache_if_gateclk_en` = `pcgen_chgflw_short || !ifctrl_pcgen_stall_short` (see `ct_ifu_pcgen.v`). The wolf path asserts `ifctrl_pcgen_stall(_short)` earlier, suppressing tag-array read enable.
- Results/next steps:
  - Next: inspect why `ifctrl_pcgen_stall(_short)` asserts earlier in wolf at t=255425 (inputs from `ipctrl_ifctrl_stall(_short)` are already known to diverge). Continue walking upstream to find the earliest signal that differs before the stall gating (likely in IFDP/ICache tag-read timing or stall generation around `ip_refill_pre`).

## 2026-02-08 15:54
- Command:
  - `python3 tools/fst_roi/fst_roi.py --fst build/logs/c910/c910_{ref,wolf}_coremark_20260208_104416.fst --signals-file /tmp/c910_ifu_sigs.txt --t0 255300 --t1 255430 --jsonl-mode fill --include-initial`
  - Inspected wolf emit of `fpga_ram` + `ct_f_spsram_512x59` in `tests/data/openc910/smart_run/work_wolf/wolf_emit/sim_top_wolf.sv`.
- Logs/FSTs: same as 2026-02-08 11:02 run.
- Equivalence: Not equivalent.
- Evidence:
  - Earliest divergence in the IFU tag path is at t=255423: `x_ct_ifu_ifdp.icache_tag_way0_28_24_hit` and `x_ct_ifu_ifdp.ifdp_icache_way0_28_24_hit` are 1 in ref but 0 in wolf. This precedes the later stall and PCGEN differences.
  - Wolf’s `fpga_ram` model (used inside `ct_f_spsram_512x59` for the I-cache tag array) introduces an extra cycle of read latency: it updates an internal `__mem_data_0` on the clock edge and then assigns `PortADataOut <= __mem_data_0`, which uses the *previous* cycle’s value. The original `fpga_ram.v` outputs `mem[PortAAddr]` in the same clock edge when not writing.
- Hypotheses:
  - The extra cycle in `fpga_ram` read behavior delays `icache_ifu_tag_dout`, shifting `icache_tag_way0_28_24_hit` by 1 cycle. This matches the observed 2-tick delay and triggers the chain of stalls and PC divergence.
- Results/next steps:
  - Next: confirm that the `fpga_ram` lowering in `src/convert.cpp` (memory modeling) matches the original semantics (read data should reflect `mem[addr]` in the same edge when `PortAWriteEnable` is 0). If confirmed, adjust the memory-lowering transform to remove the extra read latency and re-run the diff.

## 2026-02-08 18:05
- Command:
  - `make -C tests/data/openc910/bug_cases/case_032 run_c910_bug_case_ref`
  - `make -C tests/data/openc910/bug_cases/case_032 run_c910_bug_case`
  - `ctest --test-dir build --output-on-failure`
- Logs/FSTs: `build/c910_bug_case/case_032/{rtl,wolf}`; new wolf emit at `build/c910_bug_case/case_032/wolf/wolf_emit.sv`.
- Equivalence: Fixed for the minimal repro.
- Evidence:
  - The wolf emit for `fpga_ram` now wires the read port directly (no `mem_read_reg`), so `PortADataOut` sees `mem[PortAAddr]` on the same edge when `PortAWriteEnable=0`.
  - `case_032` now passes for both ref and wolf (100% coverage reported from DA-based line counts).
- Results/next steps:
  - Next: re-run the full C910 coremark diff to confirm the end-to-end divergence is resolved.
