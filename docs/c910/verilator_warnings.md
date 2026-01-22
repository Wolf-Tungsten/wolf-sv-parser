# C910 Verilator Warnings (coremark, verilator)

## Context
- Source: Verilator warnings captured from a coremark Verilator compile in smart_run.
- Command used (from smart_run/work):

```bash
verilator -O3 -x-assign 0 -Wno-fatal --threads 4 --no-timing -cc --exe --top-module sim_top \
  -f ../logical/filelists/ip.fl -f ../logical/filelists/smart.fl -f ../logical/filelists/tb_verilator.fl \
  +define+NO_DUMP
```

## Summary
- Total warnings: 74
- CASEINCOMPLETE: 3
- CMPCONST: 3
- IMPLICIT: 19
- LATCH: 1
- MULTIDRIVEN: 1
- UNSIGNED: 4
- WIDTHEXPAND: 36
- WIDTHTRUNC: 7

## Inventory (by warning type)
### CASEINCOMPLETE
- `tests/data/openc910/smart_run/logical/apb/apb_bridge.v:203:1` Case values incompletely covered (example pattern 0x5)
- `tests/data/openc910/smart_run/logical/common/timer.v:211:7` Case values incompletely covered (example pattern 0x1)
- `tests/data/openc910/smart_run/logical/pmu/pmu.v:391:3` Case values incompletely covered (example pattern 0x5)

### CMPCONST
- `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v:376:50` Comparison is constant due to limited range
- `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v:501:61` Comparison is constant due to limited range
- `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v:601:50` Comparison is constant due to limited range

### IMPLICIT
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/l2c/rtl/ct_l2c_wb.v:251:25` Signal definition not found, creating implicitly: 'rfifo_full'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v:1451:27` Signal definition not found, creating implicitly: 'wd_sel_fifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v:1889:27` Signal definition not found, creating implicitly: 'l2c_sel_fifo_full'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v:1890:27` Signal definition not found, creating implicitly: 'l2c_sel_fifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v:2026:27` Signal definition not found, creating implicitly: 'ebiu_sel_fifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_bmbif_kid.v:162:25` Signal definition not found, creating implicitly: 'xx_fifo_full'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_bmbif_kid.v:163:25` Signal definition not found, creating implicitly: 'xx_fifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1259:28` Signal definition not found, creating implicitly: 'wd_wns_dfifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1295:28` Signal definition not found, creating implicitly: 'wd_ws_dfifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1351:28` Signal definition not found, creating implicitly: 'cd_dfifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1459:25` Signal definition not found, creating implicitly: 'ac_dfifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1537:25` Signal definition not found, creating implicitly: 'cr_dfifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1596:25` Signal definition not found, creating implicitly: 'rspq_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1637:25` Signal definition not found, creating implicitly: 'cd_sid_fifo_full_fake'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1638:25` Signal definition not found, creating implicitly: 'cd_sid_fifo_empty'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ctcq.v:1244:25` Signal definition not found, creating implicitly: 'dvm_rfifo_pop_bus_vld'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ctcq.v:1245:25` Signal definition not found, creating implicitly: 'dvm_rfifo_full'
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ctcq.v:1246:25` Signal definition not found, creating implicitly: 'dvm_rfifo_empty'
- `tests/data/openc910/smart_run/logical/common/wid_for_axi4.v:76:8` Signal definition not found, creating implicitly: 'create_en'

### LATCH
- `tests/data/openc910/smart_run/logical/pmu/pmu.v:390:1` Latch inferred for signal 'sim_top.x_soc.x_apb.x_pmu.next_state' (not all control paths of combinational always assign a value)

### MULTIDRIVEN
- `tests/data/openc910/smart_run/logical/common/cpu_sub_system_axi.v:169:26` Bits [39:32] of signal 'pad_plic_int_vld' have multiple combinational drivers. This can cause performance degradation.

### UNSIGNED
- `tests/data/openc910/smart_run/logical/axi/axi_fifo.v:205:43` Comparison is constant due to unsigned arithmetic
- `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v:373:26` Comparison is constant due to unsigned arithmetic
- `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v:498:31` Comparison is constant due to unsigned arithmetic
- `tests/data/openc910/smart_run/logical/axi/axi_interconnect128.v:598:26` Comparison is constant due to unsigned arithmetic

### WIDTHEXPAND
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_kid_busif.v:67:49` Operator AND expects 32 bits on the RHS, but RHS's CONST '7'h7f' generates 7 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_kid_busif.v:67:58` Operator ADD expects 32 bits on the RHS, but RHS's CONST '7'h1' generates 7 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_fifo.v:91:51` Operator EQ expects 32 or 2 bits on the LHS, but LHS's SEL generates 1 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_fifo.v:91:51` Operator EQ expects 32 or 4 bits on the LHS, but LHS's SEL generates 3 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_fifo.v:91:51` Operator EQ expects 32 or 3 bits on the LHS, but LHS's SEL generates 2 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_fifo.v:91:51` Operator EQ expects 32 or 5 bits on the LHS, but LHS's SEL generates 4 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_snoop_req_arbiter.v:314:57` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_snoop_req_arbiter.v:312:54` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ifu/rtl/ct_ifu_ipb.v:670:53` Operator ADD expects 34 bits on the RHS, but RHS's CONST '26'h1' generates 26 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_32to1_arb.v:99:53` Operator AND expects 32 bits on the RHS, but RHS's REPLICATE generates 10 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_arb_ctrl.v:195:42` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 3 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v:722:56` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 4 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v:720:52` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 4 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v:741:51` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 4 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_kid_busif.v:406:74` Operator GTE expects 32 bits on the LHS, but LHS's SEL generates 9 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_kid_busif.v:467:57` Operator GTE expects 32 bits on the LHS, but LHS's SEL generates 10 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hreg_busif.v:207:17` Operator ADD expects 32 bits on the LHS, but LHS's VARREF 'HART_NUM' generates 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hreg_busif.v:210:17` Operator ADD expects 32 bits on the LHS, but LHS's VARREF 'HART_NUM' generates 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hreg_busif.v:398:65` Operator GTE expects 32 bits on the LHS, but LHS's SEL generates 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1583:52` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 2 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1604:52` Operator EQ expects 32 bits on the LHS, but LHS's SEL generates 2 bits.
- `tests/data/openc910/smart_run/logical/gpio/gpio_apbif.v:158:14` Operator ASSIGN expects 32 bits on the Assign RHS, but Assign RHS's REPLICATE generates 31 bits.
- `tests/data/openc910/smart_run/logical/uart/uart_receive.v:240:19` Operator ASSIGNDLY expects 3 bits on the Assign RHS, but Assign RHS's CONST '2'h0' generates 2 bits.
- `tests/data/openc910/smart_run/logical/uart/uart_receive.v:244:20` Operator ASSIGNDLY expects 3 bits on the Assign RHS, but Assign RHS's CONST '2'h0' generates 2 bits.
- `tests/data/openc910/smart_run/logical/uart/uart_trans.v:173:16` Operator ASSIGNDLY expects 3 bits on the Assign RHS, but Assign RHS's CONST '2'h0' generates 2 bits.
- `tests/data/openc910/smart_run/logical/uart/uart_trans.v:177:18` Operator ASSIGNDLY expects 3 bits on the Assign RHS, but Assign RHS's CONST '2'h0' generates 2 bits.
- `tests/data/openc910/smart_run/logical/uart/uart_baud_gen.v:105:23` Operator ASSIGNDLY expects 16 bits on the Assign RHS, but Assign RHS's CONST '1'h1' generates 1 bits.
- `tests/data/openc910/smart_run/logical/uart/uart_apb_reg.v:365:31` Operator ASSIGN expects 8 bits on the Assign RHS, but Assign RHS's REPLICATE generates 7 bits.
- `tests/data/openc910/smart_run/logical/common/wid_for_axi4.v:131:17` Operator ASSIGN expects 8 bits on the Assign RHS, but Assign RHS's CONST '5'h0' generates 5 bits.
- `tests/data/openc910/smart_run/logical/ahb/ahb.v:241:65` Operator GTE expects 40 bits on the RHS, but RHS's CONST '32'h1f000000' generates 32 bits.
- `tests/data/openc910/smart_run/logical/ahb/ahb.v:241:100` Operator LTE expects 40 bits on the RHS, but RHS's CONST '32'h1f01ffff' generates 32 bits.
- `tests/data/openc910/smart_run/logical/ahb/ahb.v:242:65` Operator GTE expects 40 bits on the RHS, but RHS's CONST '32'h10000000' generates 32 bits.
- `tests/data/openc910/smart_run/logical/ahb/ahb.v:242:100` Operator LTE expects 40 bits on the RHS, but RHS's CONST '32'h1effffff' generates 32 bits.
- `tests/data/openc910/smart_run/logical/axi/axi_err128.v:371:40` Operator SUB expects 40 bits on the RHS, but RHS's CONST '6'h30' generates 6 bits.
- `tests/data/openc910/smart_run/logical/axi/axi_err128.v:376:40` Operator SUB expects 40 bits on the RHS, but RHS's CONST '5'h10' generates 5 bits.
- `tests/data/openc910/smart_run/logical/axi/axi_err128.v:380:40` Operator ADD expects 40 bits on the RHS, but RHS's CONST '5'h10' generates 5 bits.

### WIDTHTRUNC
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_32to1_arb.v:99:38` Operator ASSIGNW expects 10 bits on the Assign RHS, but Assign RHS's AND generates 32 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/csky_apb_1tox_matrix.v:152:64` Bit extraction of var[2:0] requires 2 bit index, not 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/had/rtl/ct_had_dbg_info.v:435:32` Operator ASSIGNDLY expects 3 bits on the Assign RHS, but Assign RHS's ADD generates 4 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hreg_busif.v:743:90` Bit extraction of var[3:0] requires 2 bit index, not 5 bits.
- `tests/data/openc910/C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hreg_busif.v:747:90` Bit extraction of var[3:0] requires 2 bit index, not 5 bits.
- `tests/data/openc910/smart_run/logical/pmu/pmu.v:221:9` Logical operator LOGNOT expects 1 bit on the LHS, but LHS's SEL generates 12 bits.
- `tests/data/openc910/smart_run/logical/pmu/pmu.v:242:6` Logical operator LOGNOT expects 1 bit on the LHS, but LHS's SEL generates 12 bits.

## Risk Notes
- IMPLICIT nets can hide typos or missing signal declarations; treat as high risk until verified.
- MULTIDRIVEN may cause X-propagation or mismatched behavior between simulators; needs cleanup.
- LATCH indicates a missing default assignment in combinational logic; likely unintended.
- UNSIGNED/CMPCONST warnings can indicate constant comparisons due to width/sign issues; verify address decode logic.
- WIDTHEXPAND/WIDTHTRUNC can be benign but may mask truncation or sign bugs in datapaths and address logic.
- CASEINCOMPLETE may be intentional for sequential state machines, but should be explicit with defaults.

## Revision Plan
1) Triage and ownership
   - Split warnings into: local RTL (smart_run/logical) vs vendor RTL (C910_RTL_FACTORY).
   - For vendor RTL, decide whether to patch locally or maintain a lint-waiver list with justification.
2) Fix correctness-risk warnings first
   - IMPLICIT: add explicit declarations or fix port connection typos in the CIU/PIU/L2C blocks; ensure signals are wired as intended.
   - MULTIDRIVEN (`cpu_sub_system_axi.v`): consolidate assignments to `pad_plic_int_vld` into a single driver or a clean mux with defaults.
   - LATCH (`pmu.v`): add default assignments in the combinational `always @(*)` or refactor into `always_comb` with full coverage.
3) Address decode and width/sign cleanup
   - UNSIGNED/CMPCONST: review address range checks in `axi_fifo.v` and `axi_interconnect128.v`; cast/extend to consistent widths and signedness.
   - WIDTHEXPAND/WIDTHTRUNC: update constants and slices to explicit widths (e.g., use sized literals, casts, or localparams).
4) Case coverage hygiene
   - CASEINCOMPLETE: add `default` branches or `unique case` as appropriate; document intentional partial coverage.
5) Re-validate
   - Re-run `make runcase CASE=coremark SIM=verilator` and confirm warning count reduction.
   - If any warnings remain by design, add localized `verilator lint_off` blocks with comments explaining why.
