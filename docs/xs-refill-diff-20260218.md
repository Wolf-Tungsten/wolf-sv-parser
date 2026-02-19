# XS Refill Diff 追踪记录 (2026-02-18)

本记录覆盖基于 `build/logs/xs/xs_wolf_20260218_230019.fst` 的 RAM trace 提取与复现结果。

## 目标
- 对 “Refill test failed” 相关路径的 RAM 行为做最小化回放，对比 ref vs wolf 的读出是否一致。

## 环境与输入
- FST: `build/logs/xs/xs_wolf_20260218_230019.fst`
- 触发日志: `build/logs/xs/xs_wolf_20260218_230019.log`
- 复现用例: `tests/data/xs-bugcase/CASE_019`

## 提取与回放结论

### 1) icache nodeIn_d_q.ram_ext
- 实例路径：
  - `TOP.SimTop.cpu.l_soc.core_with_l2.core.memBlock.inner_frontendBridge.icache.nodeIn_d_q.ram_ext`
- 提取信号：
  - `R0_addr/R0_en/R0_clk/W0_addr/W0_en/W0_clk/W0_data`
- 提取方式：
  - `fst_roi.py --match-strip-width --t0 2 --t1 16606 --jsonl-mode event --no-meta`
- 结果：
  - `tests/data/xs-bugcase/CASE_019/trace_inputs.jsonl`
  - 回放 `make -C tests/data/xs-bugcase/CASE_019 run` 输出：`done mismatches=0`

### 2) slice_0 refillUnit_io_sinkD_q.ram_ext
- 实例路径：
  - `TOP.SimTop.cpu.l_soc.core_with_l2.l2top.inner_l2cache.slices_0.refillUnit_io_sinkD_q.ram_ext`
- 提取信号：
  - `R0_addr/R0_en/R0_clk/W0_addr/W0_en/W0_clk/W0_data`
- 提取方式：
  - `fst_roi.py --match-strip-width --t0 0 --t1 16607 --jsonl-mode event --no-meta`
  - `t0/t1` 覆盖整个 FST 时间范围（`_last_time` = 16607）
- 结果：
  - `tests/data/xs-bugcase/CASE_019/trace_inputs.jsonl`
  - 回放 `make -C tests/data/xs-bugcase/CASE_019 run` 输出：`done mismatches=0`

## 阶段性结论
- 以上两条 RAM 实例在当前 trace 回放下 ref/wolf 读出一致，未复现读差异。
- 若要继续缩小范围，需要明确 “Refill test failed” 对应的具体 slice 或上游模块路径（可能不是 slice_0）。
