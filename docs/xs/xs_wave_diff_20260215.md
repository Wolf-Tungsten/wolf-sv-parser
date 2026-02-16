# XS 波形差异报告（2026-02-15）

## 日志证据
- ref：CoreMark 完成并达到 cycle limit，无 refill 错误。
  - `build/logs/xs/xs_ref_20260215_121610.log:9-13`
- wolf：多条 refill mismatch，均为 `cacheid=0`（ICacheMissUnit），`mask=ff`。
  - `build/logs/xs/xs_wolf_20260215_121610.log:9-28`
  - 示例（addr 0x800000c0）：gold 行全对，core 行下半错、上半对，符合半行错序。

## FST 状态（最新）
- ref：`build/logs/xs/xs_ref_20260215_171313.fst` end_time/max_time = 100109（约 2 * 50000 周期）
- wolf：`build/logs/xs/xs_wolf_20260215_171313.fst` end_time/max_time = 16607（约 2 * 8300 周期）

## ICacheMissUnit 相关 RTL 证据
- ref：`build/xs/rtl/rtl/ICacheMissUnit.sv:370-371`（cacheid=0）
- refill 打包顺序：`build/xs/rtl/rtl/ICacheMissUnit.sv:1378-1391`
- `_GEN_3` 选择：`build/xs/rtl/rtl/ICacheMissUnit.sv:580-596 / 658`
- wolf emit：`build/xs/wolf/wolf_emit/wolf_emit.sv:1458271 / 1460584 / 1460658-1460664`

当前波形表明数据在进入 ICacheMissUnit 前已错误，优先排查 L2 返回路径。

## 上游追踪更新：L2 memory port -> SoCMisc buffer（2026-02-16）
记录时间：2026-02-16 19:08:11 +0800

**最早有效差异（valid=1）** 出现在 L2 memory port D-channel 返回数据：

1) **L2 memory port D 输出最早不一致**
- `TOP.SimTop.cpu.l_soc.core_with_l2.auto_l2top_inner_memory_port_out_d_bits_data`
- 首次差异：**t=16550**，`auto_l2top_inner_memory_port_out_d_valid=1`（ref/wolf 均为 1）

2) **SoCMisc buffer 输入已不一致**
- `TOP.SimTop.cpu.l_soc.socMisc.buffer.auto_in_d_bits_data`
- t=16548：ref 非零、wolf 全 0；`auto_in_d_valid=1`（ref/wolf 均为 1）

3) **L3 边界反映相同错误数据**
- `TOP.SimTop.cpu.l_soc.l3cacheOpt.auto_in_0_d_bits_data` @ t=16566
- 与 `socMisc.auto_xbar_out_1_d_bits_data` / `l3cacheOpt.slices_0.sourceD.io_d_bits_data` 一致

### 结论（更新）
最早差异在 **L2 memory port 的 D-channel 数据**，有效握手已成立。根因更可能在 **L2 memory port 返回路径或其上游 fabric**，而非 SoCMisc 或 L3。

## 上游追踪更新：inner_l2cache 输入差异（2026-02-16）
记录时间：2026-02-16 19:08:11 +0800

对 FST 中可见的 `inner_l2cache` 输入信号（105 个）进行对比后发现不一致：

- **最早输入差异**：
  - `TOP.SimTop.cpu.l_soc.core_with_l2.l2top.inner_l2cache.auto_in_1_a_valid`
  - 时间：**t=1472**（ref=0, wolf=1；`--ignore-x`）

### 驱动链路（L2Top 连接关系）
- `inner_l2cache.auto_in_1_a_valid` ← `_inner_buffer_auto_out_1_a_valid`
  - `build/xs/rtl/rtl/L2Top.sv:3565`
- `_inner_buffer_auto_out_1_a_valid` 为 `TLBuffer_27 inner_buffer` 的输出
  - `build/xs/rtl/rtl/L2Top.sv:2655`, `build/xs/rtl/rtl/L2Top.sv:2977`
- `inner_buffer.auto_in_1_a_valid` 来自 `_inner_xbar_auto_out_1_a_valid`
  - `build/xs/rtl/rtl/L2Top.sv:2765`

结论：`inner_l2cache` **输入已不一致**，差异在进入 `inner_l2cache` 前即存在，路径为 **inner_xbar → inner_buffer → inner_l2cache**。

## 上游追踪更新：inner_xbar 上游模块（2026-02-16）
记录时间：2026-02-16 19:15:25 +0800

### 上游模块识别
`inner_xbar.auto_in_1_a_valid` 的直接上游是 **`inner_busPMU`（`BusPerfMonitor_1` 实例）**：
- `inner_xbar.auto_in_1_a_valid` ← `_inner_busPMU_auto_out_1_a_valid`
  - `build/xs/rtl/rtl/L2Top.sv:1919-1940`
- `inner_busPMU` 实例声明：
  - `build/xs/rtl/rtl/L2Top.sv:2362`

### 上游模块输入差异
对 FST 中可见的 `inner_busPMU` 输入信号（34 个）做对比，存在差异：
- **最早输入差异**：
  - `TOP.SimTop.cpu.l_soc.core_with_l2.l2top.inner_busPMU.auto_in_1_a_valid`
  - 时间：**t=1470**（ref=0, wolf=1；`--ignore-x`）
- 该输入来自 `inner_logger_1`：
  - `inner_busPMU.auto_in_1_a_valid` ← `_inner_logger_1_auto_out_a_valid`
  - `build/xs/rtl/rtl/L2Top.sv:2368-2390`

结论：`inner_xbar` 的输入差异来自 **`inner_busPMU` 输出**，而 `inner_busPMU` **输入已在更上游出现差异**。

## 上游追踪更新：inner_logger_1 输入差异（2026-02-16）
记录时间：2026-02-16 19:18:10 +0800

`inner_logger_1` 的输入来自 L2Top 顶层端口 `auto_inner_logger_in_1_*`，对 FST 中可见的 7 个输入信号做对比，出现差异：
- **最早输入差异**：
  - `TOP.SimTop.cpu.l_soc.core_with_l2.l2top.inner_logger_1.auto_in_a_valid`
  - 时间：**t=1470**（ref=0, wolf=1；`--ignore-x`）

结论：`inner_logger_1` 的输入在 **t=1470** 已出现差异，说明差异来自 L2Top 更上游的 `auto_inner_logger_in_1_*` 端口源头，需要进一步追踪该端口在 SoC 顶层的连接路径。

## 上游追踪更新：Frontend/ICache 输出差异（2026-02-16）
记录时间：2026-02-16 19:29:39 +0800

对 ICache client 输出与 FrontendBridge icache_in 进行对比后，发现更早的差异点：

1) **Frontend ICache client 输出最早差异**
- `TOP.SimTop.cpu.l_soc.core_with_l2.core.frontend.auto_inner_icache_client_out_a_valid`
  - t=1466：ref=0，wolf=1（`--ignore-x`）
- `TOP.SimTop.cpu.l_soc.core_with_l2.core.frontend.auto_inner_icache_client_out_a_bits_address`
  - t=1466：ref=`000000000000000010000000000000000000000001000000`
  - wolf=`000000000000000010000000000000000000001001000000`

2) **MemBlock → FrontendBridge icache_in 同步差异**
- `TOP.SimTop.cpu.l_soc.core_with_l2.core.memBlock.auto_inner_frontendBridge_icache_in_a_valid`
  - t=1466：ref=0，wolf=1（`--ignore-x`）
- `TOP.SimTop.cpu.l_soc.core_with_l2.core.memBlock.auto_inner_frontendBridge_icache_in_a_bits_address`
  - t=1466：地址与 Frontend ICache client 输出一致的差异

3) **FrontendBridge icache_out 为更晚差异**
- `TOP.SimTop.cpu.l_soc.core_with_l2.core.auto_memBlock_inner_frontendBridge_icache_out_a_valid`
  - t=1470：ref=0，wolf=1（`--ignore-x`）
- 说明差异先于 FrontendBridge 的 icache_out，且从 ICache client 输出一路向下游传播。

4) **已检查的 ICache 输入未见差异**
- `auto_inner_icache_ctrlUnitOpt_in_a_*` 在 t=0–2000 内无差异（valid/address/opcode/source）。

结论：差异最早出现在 **Frontend 内 ICache client 输出**，并同步反映到 MemBlock 的 FrontendBridge icache_in 端口；但是否由 ICache 内部状态引起仍需结合其它输入路径进一步确认。

## 上游追踪补充：FTQ 预取输入差异（2026-02-16）
记录时间：2026-02-16 20:11:50 +0800

对 `inner_icache` 的 FTQ 输入进行补查后发现更早差异：

- `TOP.SimTop.cpu.l_soc.core_with_l2.core.frontend.inner_icache.io_fromFtq_prefetchReq_bits_nextCachelineVAddr_addr`
  - t=1156：ref/wolf 地址不同（`--ignore-x`）

修正结论：此前“可能来自 ICache 内部状态”的推测不充分，因为 **FTQ 预取输入已在更早时间产生差异**；需优先沿 FTQ/Frontend 上游路径继续追踪，并补查 `inner_icache` 其它输入端口的一致性。

## 追踪进展：开始定位 FTQ 路径（2026-02-16）
记录时间：2026-02-16 20:26:42 +0800

已确认 `inner_icache.io_fromFtq_prefetchReq_*` 在 t=1156 出现差异，后续追踪将以 FTQ/Frontend 上游链路为主线，优先定位产生该预取地址差异的源头。

## FTQ 输入一致性分析（2026-02-16）
记录时间：2026-02-16 20:32:06 +0800

使用 `fst_diff_tool.py` 对 `inner_ftq` 的 **429 个输入端口** 进行对比（t=0–2000，`--ignore-x`，`--strip-width`），结果如下：

- **发现差异的输入：21 个**（其余 **408 个** 在该时间窗内一致）。
- **最早差异时间点：t=1156**。

### 具体差异信号（按类别）
1) **来自 BPU 预测输入**
- `io_fromBpu_prediction_bits_startPc_addr` @ t=1156
- `io_fromBpu_prediction_bits_target_addr` @ t=1156

2) **来自 BPU meta / resolveMeta**
- `io_fromBpu_meta_valid` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_ittage_allocate_bits` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_0` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_1` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predBiasIdx` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_0` @ t=1158
- `io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_1` @ t=1158

3) **来自 BPU perfMeta / s3FtqPtr**
- `io_fromBpu_perfMeta_s1Prediction_target_addr` @ t=1158
- `io_fromBpu_perfMeta_s3Prediction_target_addr` @ t=1158
- `io_fromBpu_s3FtqPtr_value` @ t=1158
- `io_fromBpu_s3FtqPtr_flag` @ t=1550

4) **来自 ICache 的 ready**
- `io_toICache_prefetchReq_ready` @ t=1206

5) **来自 Backend 的 commit / callRetCommit**
- `io_fromBackend_callRetCommit_0_valid` @ t=1430
- `io_fromBackend_callRetCommit_0_bits_ftqPtr_value` @ t=1430
- `io_fromBackend_callRetCommit_0_bits_rasAction` @ t=1430
- `io_fromBackend_commit_valid` @ t=1430
- `io_fromBackend_commit_bits_value` @ t=1430

结论：在 t=0–2000 内，FTQ 输入差异主要集中于 **BPU 预测/元数据路径**，且最早差异与 BPU 预测地址相关；FTQ 输入并非完全一致，需要沿 BPU/Backend/ICache ready 路径继续追踪。

## 计划：下一步追踪 BPU 路径（2026-02-16）
记录时间：2026-02-16 21:01:23 +0800

已确认 FTQ 输入差异最早来自 BPU 预测/元数据相关信号，下一步计划以 **BPU 上游路径** 为主线继续追踪差异源头，并同步检查与 BPU 相关的 redirect/commit 交互是否一致。
