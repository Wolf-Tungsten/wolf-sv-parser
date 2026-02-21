# XS Bugcase 006：Refill test failed（ICache refill 数据不一致）

## 摘要
`xs_wolf_20260218_230019.log` 出现 `Refill test failed`，`cacheid=0` 且地址集中在 `0x8000_0000` 附近，表明 **ICache refill 数据与 golden memory 不一致**。
日志中多条相邻 cache line 的 `mask=ff`，但 `Core` 数据出现 **顺序错位/部分为 0**，初步怀疑 refill 数据打包或 beat 顺序问题（而非地址对齐问题）。

## 复现与日志证据
- 运行日志：`build/logs/xs/xs_wolf_20260218_230019.log`
- 波形：`build/logs/xs/xs_wolf_20260218_230019.fst`
- 关键日志片段（RefillChecker 打印）：
  - `cacheid=0,mask=ff,realpaddr=0x800000c0: Refill test failed!`
  - `cacheid=0,mask=ff,realpaddr=0x80000080: Refill test failed!`
  - `cacheid=0,mask=ff,realpaddr=0x80000040: Refill test failed!`
  - `cacheid=0,mask=ff,realpaddr=0x80000000: Refill test failed!`
  - `cacheid=0,mask=ff,realpaddr=0x800001c0: Refill test failed!`

## Difftest/Refill 结构要点
- `DiffRefillEvent` 定义：`addr` + `data(8×64b)` + `mask(8b)`。
  - `tests/data/xiangshan/difftest/src/main/scala/Bundles.scala`
- `RefillChecker` 比对逻辑：
  - 以 `addr` 向下 64B 对齐作为 cache line 基址，逐 beat 比对 `data[i]` 与 `goldenmem`。
  - `mask` 只控制对应 beat 是否参与比对。
  - 发生不一致会打印 `cacheid/mask/realpaddr` 与 `Gold/Core` 数据。
  - `tests/data/xiangshan/difftest/src/test/csrc/difftest/checkers/refill.cpp`

## cacheid 对应模块（DifftestWithIndex）
`cacheid` 来自 `DiffRefillEvent.index`，在 XiangShan 中由不同模块填写：
- `0`：ICache MissUnit refill（`ICacheMissUnit.scala`）
  - `tests/data/xiangshan/src/main/scala/xiangshan/frontend/icache/ICacheMissUnit.scala`
- `1`：DCache MissQueue refill（`MissQueue.scala`）
  - `tests/data/xiangshan/src/main/scala/xiangshan/cache/dcache/mainpipe/MissQueue.scala`
- `2`：L2TLB refill（`L2TLB.scala`）
  - `tests/data/xiangshan/src/main/scala/xiangshan/cache/mmu/L2TLB.scala`
- `3/4`：ICache MainPipe refill（但当前 difftest.valid 为 `false.B`）
  - `tests/data/xiangshan/src/main/scala/xiangshan/frontend/icache/ICacheMainPipe.scala`

**本次日志均为 `cacheid=0`，优先定位 ICache MissUnit 的 refill 数据路径。**

## 可能存在问题的信号（优先级从高到低）

### 1) ICache MissUnit → DiffRefillEvent（最可能）
对应 `ICacheMissUnit.scala` 的 difftest 绑定：
- `difftest.valid := writeSramValid`
- `difftest.addr := Cat(mshrInfo.blkPAddr, 0.U(blockOffBits.W))`
- `difftest.data := respDataReg.asTypeOf(difftest.data)`
- `difftest.mask := 0xff`

重点信号：
- `respDataReg`（512b line data）
  - 是否在 wolf emit 中被 **错误的 Vec 打包顺序**（如 `[7:0]` 反向，或分 bank 重新拼接顺序错误）。
- `writeSramValid` / `respDataReg` 时序关系
  - `valid` 提前导致只捕到部分 beat，产生 **中间 beat 为 0** 的症状。
- `mshrInfo.blkPAddr`
  - 虽然 `realpaddr` 已是 64B 对齐，但仍需确认对齐拼接未被改写（尤其是 `blockOffBits` 在 emit/transform 中的常量传播）。

#### cacheid=0 追加关注点（ICache MissUnit 内部）
以下信号直接决定 refill 数据的 beat 顺序与拼接：
- `readBeatCnt` / `waitLast`：`respDataReg(readBeatCnt) := io.memGrant.bits.data`
  - 若 wolf 在 **动态索引写 Vec** 时错误降级（例如缺失 `idx * beatBits`），会造成 beat 交叉或覆盖。
  - 该模式与 `docs/xs/xs_bugcase_005.md` 的 packed 动态索引问题相似，需重点排查。
- `io.memGrant.bits.data`（TL-D beat 数据）与 `respDataReg` 的 mapping
  - 如果 `respDataReg` 在 emit 中被 flatten 成 packed 向量，注意 `asTypeOf(difftest.data)` 的端序是否与 `asUInt` 一致。
- `lastFire` / `lastFireNext` / `mshrInfo` 采样
  - `mshrInfo` 通过 `RegEnable(..., lastFire)` 锁存，如果 `lastFire` 与 `writeSramValid` 不同步，可能造成 `addr/data` 不匹配。

### 2) DCache MissQueue（cacheid=1 时再核查）
虽未在本日志出现，但建议排查以下信号以防混用：
- `io.refill_to_ldq.bits.data_raw`
- `io.refill_to_ldq.bits.hasdata`
- `io.refill_to_ldq.bits.refill_done`
- `io.refill_to_ldq.bits.addr`
路径：`tests/data/xiangshan/src/main/scala/xiangshan/cache/dcache/mainpipe/MissQueue.scala`

### 3) L2TLB Refill（cacheid=2 时再核查）
若出现 `cacheid=2`，关注：
- `refill_data`、`difftest_ptw_addr` 打包顺序
路径：`tests/data/xiangshan/src/main/scala/xiangshan/cache/mmu/L2TLB.scala`

### 4) Difftest 接口打包
- `RefillEvent` 的 `Vec(8, UInt(64.W))` 在 Verilog 展开时的 **端序/索引** 是否被 wolf emit 重排。
- 若出现 `mask` 被截断或位序反转，也会造成不一致，但本例 `mask=ff` 表明更多是 data 顺序/时序问题。

## 初步判断
- 多条相邻 cache line (`0x8000_0000` ~ `0x8000_01c0`) 同时不一致，且 `mask=ff`。
- `Core` 数据中存在 **块内 beat 交叉、错位或 0 填充**，不符合单纯地址对齐错误的特征。
- 结合 `cacheid=0`，最可能是 **ICache refill 数据打包或捕获时序** 出错。

## 建议的后续验证
1) 用波形确认 `refill` 事件触发时 `respDataReg` 的 512b 原始顺序，与 `difftest.data[0..7]` 的映射是否一致。
2) 若可补齐 ref 波形，对 `xs_ref_*` vs `xs_wolf_*` 运行 `tools/fst_diff_tool.py`，定位最早差异信号，再回溯到 refill 数据路径。

## ICache MissUnit 输入对比（ref vs wolf）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/icache_missunit_inputs_ref_20260218_225315.txt`
- `out/icache_missunit_inputs_wolf_20260218_230019.txt`

差异只出现在 `io_memGrant_bits_data`（TL-D 数据），其余输入信号保持一致：
- 差异周期（rise）：cycle 8283 → 8302（连续）
- 该区间内 `io_memGrant_valid` 在部分周期为 1，且对应到后续 difftest_refill mismatch 的地址序列。

说明 ref/wolf 在 **TL-D 返还数据** 上已出现差异，进一步印证问题可能发生在 MissUnit 上游或 TL-D 数据路径，而非 MissUnit 的输入控制信号。

## ICache MissUnit 输出对比（ref vs wolf）
对以下两个文件进行 diff（cycle 0 → 8250），结果 **无差异**：
- `out/icache_missunit_outputs_ref_20260218_225315.txt`
- `out/icache_missunit_outputs_wolf_20260218_230019.txt`

说明在相同周期范围内，MissUnit 对外的主要输出（`io_resp`、`io_metaWrite`、`io_dataWrite`、`io_memAcquire` 等）与 ref 一致。
在输入/输出均一致的情况下，出现 `Refill test failed` 更可能与以下位置相关：
- `difftest_refill_*` 在 MissUnit 内部的采样/拼接（不经过 io_* 端口），或
- difftest gateway/打包路径在 emit 中的顺序/位宽处理。

## ICache MissUnit difftest_refill_* 公共信号对比（ref vs wolf）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/icache_missunit_difftest_refill_common_ref_20260218_225315.txt`
- `out/icache_missunit_difftest_refill_common_wolf_20260218_230019.txt`

差异集中在 `difftest_refill_data_1/2/5/6`，其余字段（`valid/addr/data_0/3/4/7/coreid`）一致。
出现差异的 `valid=1` 周期与地址（rise，相同差异在 fall 重复）：
- cycle 8285，addr `0x800000c0`
- cycle 8289，addr `0x80000080`
- cycle 8291，addr `0x80000040`
- cycle 8295，addr `0x80000000`
- cycle 8297，addr `0x800001c0`
- cycle 8301，addr `0x80000180`

结论：**Mismatch 已在 MissUnit 的 difftest_refill_* 处出现**，更可能源自 MissUnit 内部的 beat 写入/拼接（如 `respDataReg`、`readBeatCnt`）或上游数据顺序，而非仅发生在 gateway 之后。

## Difftest gateway refill 信号对比（ref vs wolf）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/gateway_refill_ref_20260218_225315.txt`
- `out/gateway_refill_wolf_20260218_230019.txt`

差异出现在 cycle 8295 → 8300（rise/fall 同步），地址序列对应：
- `0x80000000`
- `0x80000040`
- `0x80000080`
- `0x800000c0`

差异仅体现在 `io_bits_data_1/2/5/6`，`io_valid/io_bits_valid/addr/mask/coreid/index` 在 ref/wolf 仍一致（且 `io_valid`/`io_bits_valid` 均为 0）。
该模式与 MissUnit difftest_refill_* 的差异相吻合，说明 gateway 侧只是 **传递了上游差异**，并非唯一问题源头。

## 周期计数偏差说明（log vs waveform）
本次 wolf 运行中，波形与日志的 cycle 计数存在固定偏差：
- 日志 `xs_wolf_20260218_230019.log` 中 `cycleCnt = 8250`（abort 行）。
- 波形中 `missUnit.clock` / `endpoint.clock` 的最大 cycle 为 **8302**。
- 由此推算 **波形 cycle 计数约比日志多 +52**。

该偏差可能来自计数起点不同（复位期/启动期）或 difftest 在失败后继续运行一小段的策略；在本次 run 内可视为稳定，但跨不同 run 或时钟域不保证固定。

## MissUnit TL-D 数据上游路径（XiangShan 结构）
结合 XiangShan 架构与本工程连线，ICache MissUnit 的 TL-D 数据来源路径如下：
1) `ICacheMissUnit` 通过 TileLink `bus.d` 接收 TL-D（memGrant）  
   - `tests/data/xiangshan/src/main/scala/xiangshan/frontend/icache/ICacheImp.scala`
2) `bus` 来自 `ICache` 的 `clientNode`（TLClientNode）  
   - `tests/data/xiangshan/src/main/scala/xiangshan/frontend/icache/ICache.scala`
3) `XSCore` 将 `frontend.inner.icache.clientNode` 连接到 `memBlock.inner.frontendBridge.icache_node`  
   - `tests/data/xiangshan/src/main/scala/xiangshan/XSCore.scala`
4) `XSTile` 将 `memBlock.frontendBridge.icache_node` 接入 L2Top 的 L1I 端口（`l1i_logger`）  
   - `tests/data/xiangshan/src/main/scala/xiangshan/XSTile.scala`

因此，MissUnit 的 `io_memGrant_bits_data` 差异更可能在 **L2Top 的 L1I 端口链路或更下游的 L2/内存路径**形成，而非 MissUnit 本身的输入控制信号。

## 路径起点端口对比（memBlock.frontendBridge icache_out_d）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/icache_frontendbridge_out_d_ref_20260218_225315.txt`
- `out/icache_frontendbridge_out_d_wolf_20260218_230019.txt`

差异仅出现在 `auto_inner_frontendBridge_icache_out_d_bits_data`：
- rise 周期：`8281, 8282, 8285, 8286, 8287, 8288, 8291, 8292, 8293, 8294, 8297, 8298, 8299, 8300`
- `valid/ready/opcode/param/size/source/sink/denied/corrupt` 在 ref/wolf 仍一致

结论：**在 memBlock.frontendBridge 的 D 通道数据端口已出现数据不一致**，与 MissUnit `io_memGrant_bits_data` 的差异一致。下一步建议对比 `icache_in_d` 与 `icache_out_d`，以确认差异发生在 frontendBridge 内部还是更上游。

## ICache A 通道地址对比（frontendBridge icache_out_a）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/icache_frontendbridge_out_a_addr_ref_20260218_225315.txt`
- `out/icache_frontendbridge_out_a_addr_wolf_20260218_230019.txt`

结果：**无差异**（valid/ready/address 全部一致）。

结论：在 `frontendBridge.icache_out_a` 处，L1 发出的 A 通道地址与握手信号在 ref/wolf 保持一致，**L1 发出的请求地址正确**。

## L2 侧 logger 输入 A 通道地址对比（l2top.auto_inner_logger_in_1）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/l2top_l1i_logger_in_a_addr_ref_20260218_225315.txt`
- `out/l2top_l1i_logger_in_a_addr_wolf_20260218_230019.txt`

结果：**无差异**（valid/ready/address 全部一致）。

结论：frontendBridge 向 L2 侧（l1i logger）发出的 A 通道地址在 ref/wolf 一致，**L1→L2 的地址路径无异常**。

## L2 侧 logger 输入 D 通道数据对比（l2top.auto_inner_logger_in_1）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/l2top_l1i_logger_in_d_ref_20260218_225315.txt`
- `out/l2top_l1i_logger_in_d_wolf_20260218_230019.txt`

差异仅出现在 `auto_inner_logger_in_1_d_bits_data`：
- rise 周期：`8281, 8282, 8285, 8286, 8287, 8288, 8291, 8292, 8293, 8294, 8297, 8298, 8299, 8300`
- `valid/ready/opcode/param/size/source/sink/denied/corrupt` 在 ref/wolf 仍一致

结论：**L2 侧 logger 输入处已出现 D 通道数据不一致**，与 `frontendBridge.icache_out_d`、MissUnit `io_memGrant_bits_data` 的差异周期一致。说明异常来自 L2 侧返回数据路径，而非 L1 发出的请求地址。

## 外部 memory_port 读地址/读数据对比
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- 读地址（A 通道）：`out/l2top_memory_port_out_a_addr_ref_20260218_225315.txt`
  vs `out/l2top_memory_port_out_a_addr_wolf_20260218_230019.txt`
- 读数据（D 通道）：`out/l2top_memory_port_out_d_ref_20260218_225315.txt`
  vs `out/l2top_memory_port_out_d_wolf_20260218_230019.txt`

结果：
- **读地址无差异**（valid/ready/address 全部一致）
- **读数据有差异**，仅 `auto_l2top_inner_memory_port_out_d_bits_data` 不同
  - 差异周期（rise）：`8273` → `8302`（连续）
  - `valid/ready/opcode/param/size/source/sink/denied/corrupt` 保持一致

结论：外部 memory_port 返回的数据在 ref/wolf 已经出现不一致，而地址请求保持一致。说明异常**发生在 L2 侧向外部取数或外部返回路径**，进一步印证 D 通道数据源已在 L2 外部/更下游异常。

补充关联性说明：
- `memory_port_out_d` 的数据差异包含多段 `valid=1` 周期（例如 rise 周期：`8273, 8274, 8277, 8278, 8279, 8280, 8283, 8284, 8285, 8286, 8289, 8290, 8291, 8292, 8295, 8296, 8299, 8300`）。
- 其中 `8285/8289/8291/8295` 与 MissUnit `difftest_refill_*` 的 mismatch 周期重叠，且 `memory_port_out_a` 的地址集合覆盖 `0x80000000/40/80/c0/180/1c0`。

因此，**外部返回数据的差异与 Refill 失败高度相关**；但 D 通道不携带地址，当前关联是基于时序窗口与请求地址集合的间接证据。

## SoC 侧路径（L2 memory_port → L3）
基于 `Top.scala` / `SoC.scala` 的连线，当前配置（未启用 CHI）下：
- `XSTile.memory_port`（来自 `l2top.inner.memory_port`）  
  → `misc.core_to_l3_ports`  
  → `l3_banked_xbar`  
  → `l3_xbar`  
  → `l3cacheOpt`（HuanCun L3）  
  → `l3_out` → 外部内存/AXI

该路径可作为接下来的排查起点：需要在 `l3cacheOpt` / `l3_out` 附近抓取 D 通道数据，确认数据是在 **L3 内部** 还是 **L3 外侧** 发生偏差。

## L3 外侧端口（l3cacheOpt.auto_out_0..3）A/D 通道对比
对以下 4 个端口分别进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/l3cache_out{0..3}_a_addr_ref_20260218_225315.txt` vs `out/l3cache_out{0..3}_a_addr_wolf_20260218_230019.txt`
- `out/l3cache_out{0..3}_d_ref_20260218_225315.txt` vs `out/l3cache_out{0..3}_d_wolf_20260218_230019.txt`

结果：
- **A 通道地址**：4 个端口均无差异（valid/ready/address 全部一致）
- **D 通道数据**：4 个端口均出现差异，且只在 `*_d_bits_data` 字段
  - port0：mismatch rise 周期 `8267..8294`（valid=1 的 rise 周期：`8276, 8277, 8288, 8289, 8291, 8292, 8293, 8294`）
  - port1：mismatch rise 周期 `8267..8294`（valid=1 的 rise 周期：`8273, 8274, 8285, 8286`）
  - port2：mismatch rise 周期 `8267..8294`（valid=1 的 rise 周期：`8270, 8271, 8282, 8283`）
  - port3：mismatch rise 周期 `8267..8294`（valid=1 的 rise 周期：`8267, 8268, 8279, 8280`）

结论：**L3 对外输出的 D 通道数据在 ref/wolf 已经出现不一致，而地址与控制保持一致**。这进一步将异常定位到 L3 外侧/SoC 外部内存返回路径或其前后数据处理逻辑。

## AXI4 外侧读地址对比（l_memXbar.axi4xbar.auto_out_ar）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/axi4_mem_ar_addr_ref_20260218_225315.txt`
- `out/axi4_mem_ar_addr_wolf_20260218_230019.txt`

结果：**无差异**（valid/ready/address 全部一致）。

结论：AXI4 外侧读地址在 ref/wolf 一致，进一步说明异常集中在 **读数据返回路径**。

## AXI4 外侧读数据对比（l_memXbar.axi4xbar.auto_out_r）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/axi4_mem_r_data_ref_20260218_225315.txt`
- `out/axi4_mem_r_data_wolf_20260218_230019.txt`

差异仅出现在 `auto_out_r_bits_data`：
- mismatch rise 周期：`8258..8283`（18 个 rise 周期）
- `valid/ready/id/resp/last` 在 ref/wolf 一致

结论：**AXI4 外侧读数据已出现不一致**，且只在 data 字段，说明异常已经发生在 SoC 外部内存/AXI 返回路径或其上游（更下游的内存模型）。

## 外部内存模型（memory.ram）AR/R 对比
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- AR：`out/axi4_mem_model_ar_addr_ref_20260218_225315.txt` vs `out/axi4_mem_model_ar_addr_wolf_20260218_230019.txt`
- R：`out/axi4_mem_model_r_data_ref_20260218_225315.txt` vs `out/axi4_mem_model_r_data_wolf_20260218_230019.txt`

结果：
- **AR 无差异**（valid/ready/address 全一致）
- **R 有差异**，仅 `auto_in_r_bits_data` 不同
  - mismatch rise 周期：`8258..8283`（18 个 rise 周期）
  - `valid/ready/id/resp/last` 在 ref/wolf 一致

结论：外部内存模型返回的 R 数据已不一致，且与 AXI4 外侧 `auto_out_r_bits_data` 的差异周期一致。**异常已定位到外部内存模型/其数据生成路径**。

## 外部内存模型实现说明（AXI4RAM/AXI4Memory）
当前 FST 中出现 `TOP.SimTop.cpu.memory.ram.*` 信号，说明外部内存模型走的是 **AXI4RAM**（静态 RAM）路径，而非 DPI 动态延迟的 AXI4Memory：
- `AXI4RAM`：`tests/data/xiangshan/src/main/scala/device/AXI4RAM.scala`
  - 内部 `Mem` 或 `DifftestMem` 保存数据，`in.r.bits.data := rdata` 直接由 RAM 读出
  - 这一路对应 `TOP.SimTop.cpu.memory.ram.auto_in_*`
- `AXI4Memory`：`tests/data/xiangshan/src/main/scala/device/AXI4Memory.scala`
  - 通过 DPI-C `memory_request/memory_response` 与外部 C++ 内存模型交互（动态延迟）

结论：现有差异已落在 **AXI4RAM 返回数据** 上，下一步需要重点排查 `AXI4RAM` 的读写路径（索引计算、beat 计数、读写竞态或 wolf emit 的打包/位序）。

## 下一步排查方向（DPIC 返回数据路径）
当前走 **AXI4RAM + DifftestMem (DPIC)**。建议按以下“DPIC 返回 → AXI4RAM → SoC”路径逐级确认差异是否出现：
1) **DPIC 返回最近端**：`TOP.SimTop.cpu.memory.ram.rdata_r_0..3`（64b 切片）
2) **AXI4RAM R 通道**：`TOP.SimTop.cpu.memory.ram.auto_in_r_bits_data` / `nodeIn_r_bits_data`
3) **SoC 外侧 AXI**：`TOP.SimTop.cpu.memory.io_axi4_0_r_bits_data`
4) **SoC 内 AXI**：`TOP.SimTop.cpu.l_memXbar.axi4xbar.auto_out_r_bits_data`

若差异已在 `rdata_r_*` 层出现，则可进一步怀疑 **DPIC 内存模型/ram.h** 或其读写次序；若在 `rdata_r_*` 一致而 `auto_in_r_bits_data` 出现差异，则需检查 AXI4RAM 内部拼接/beat 计数。

## DPIC 最近端切片对比（memory.ram.rdata_r_0..3）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/axi4_mem_model_rdata_r_ref_20260218_225315.txt`
- `out/axi4_mem_model_rdata_r_wolf_20260218_230019.txt`

结果：
- 仅 `rdata_r_1` 与 `rdata_r_2` 出现差异
  - mismatch rise 周期：`8259..8284`（18 个 rise 周期）
- `rdata_r_0` / `rdata_r_3` 无差异

结论：差异已在 **DPIC 返回的 64b 切片** 层出现，且集中在中间两段（rdata_r_1/2），支持怀疑 **DPIC 内存模型或其读出顺序/拼接** 问题。

## wolf_emit 与原始 RTL 的 DPIC 调用差异（Mem1R1WHelper）
对比 `build/xs/rtl/rtl/Mem1R1WHelper.v` 与 `build/xs/wolf/wolf_emit/wolf_emit.sv`（`Mem1R1WHelper__p12579229824793630810`）：

### 1) 宏分支被移除
- 原始 RTL 存在 `DISABLE_DIFFTEST_RAM_DPIC` / `GSIM` 分支（可切换 DPI/异步读）。
- wolf_emit 直接展开为 **固定 DPI + 同步读**，`r_0_async` 恒为 `0`。

### 2) 读写时序顺序变化
- 原始 RTL：读与写在 **两个** `always @(posedge clock)`，同周期读写顺序不确定。
- wolf_emit：读/写/更新合并在 **一个** always，顺序固定为 **read → write → r_0_data <= read**。
  - 当同周期读写同地址时，wolf_emit 等价于“读先于写”，可能与原始 RTL 的非确定顺序不同。

### 3) 读值暂存方式改变
- 原始 RTL：`r_0_data <= difftest_ram_read(r_0_index);`
- wolf_emit：`_dpi_ret_0_intm = difftest_ram_read(...); r_0_data <= _dpi_ret_0;`
  - 功能等价，但与读写顺序合并后，行为更确定。

结论：wolf_emit 在 **DPIC 读写时序与宏控制** 上与原始 RTL 有差异，可能导致 `DifftestMem` 同周期读写行为不同，从而引发 `rdata_r_1/2` 的差异。

### 代码片段对比（原始 RTL vs wolf_emit）
**原始 RTL：`build/xs/rtl/rtl/Mem1R1WHelper.v`（节选）**
```verilog
`ifdef GSIM
  assign r_0_async = 1'b1;
always @(*) begin
  r_0_data = 0;
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (r_0_enable) begin
    r_0_data = difftest_ram_read(r_0_index);
  end
`endif
end
`else // GSIM
  assign r_0_async = 1'b0;
always @(posedge clock) begin
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (r_0_enable) begin
    r_0_data <= difftest_ram_read(r_0_index);
  end
`endif
end
`endif // GSIM

always @(posedge clock) begin
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (w_0_enable) begin
    difftest_ram_write(w_0_index, w_0_data, w_0_mask);
  end
`endif
end
```

**wolf_emit：`build/xs/wolf/wolf_emit/wolf_emit.sv`（节选）**
```verilog
assign r_0_async = 1'b0;
always @(posedge clock) begin
  if (r_0_enable) begin
    _dpi_ret_0_intm = difftest_ram_read(r_0_index);
  end
  if (w_0_enable) begin
    difftest_ram_write(w_0_index, w_0_data, w_0_mask);
  end
  if (r_0_enable) begin
    r_0_data <= _dpi_ret_0;
  end
end
```

**解读**：
- 原始 RTL 有 `GSIM`/`DISABLE_DIFFTEST_RAM_DPIC` 分支；wolf_emit 固定为 DPI + 同步读，`r_0_async` 恒 0。
- 原始 RTL 的读写分在两个 always 中；wolf_emit 合并为一个 always，顺序固定为 “读 → 写 → 更新寄存器”，改变同周期读写语义。

## Mem1R1WHelper 端口对比（helper_0..3）
对以下两个文件进行 diff（扩展到 wolf 最大 cycle=8302）：
- `out/axi4_mem_model_helper_r0_ref_20260218_225315.txt`
- `out/axi4_mem_model_helper_r0_wolf_20260218_230019.txt`

结果：
- 差异仅出现在 `helper_1.r_0_data` 与 `helper_2.r_0_data`
  - mismatch rise 周期：`8258..8283`（18 个 rise 周期）
- `r_0_enable` / `r_0_index` 在 ref/wolf **一致**

结论：地址与使能一致，数据仅在中间两个 helper（lane 1/2）异常，进一步支持 **DPIC 读出值或读写顺序** 问题。

## Mem1R1WHelper 读写同周期筛查（ref vs wolf）
对以下合并读写 trace 进行筛查（cycle 0 → 8302）：
- `out/mem1r1whelper_rw_ref_20260218_225315.txt`
- `out/mem1r1whelper_rw_wolf_20260218_230019.txt`

结果：
- **未发现同周期读写**（`r_0_enable=1 && w_0_enable=1`）的情况，helper_0..3 均为 0 次。
- helper_1/2 的 mismatch 全部发生在 **`r_0_enable=1, w_0_enable=0`** 的周期。
  - mismatch 周期数：18（rise 相位）
  - 起始周期：8258

观察：
- wolf 的 `r_0_data` 在 helper_1/2 上呈现 **“滞后一拍”** 的特征（例如 cycle 8259 的 wolf 值与 cycle 8258 的 ref 值一致）。
- 这更像 **DPI 读返回时序/寄存更新顺序** 问题，而非同周期写干扰读。

## Mem1R1WHelper 完整源码（ref / wolf）
**原始 RTL：`build/xs/rtl/rtl/Mem1R1WHelper.v`（完整模块）**
```verilog
`ifdef SYNTHESIS
  `define DISABLE_DIFFTEST_RAM_DPIC
`endif
module Mem1R1WHelper #(
  parameter RAM_SIZE
)(
  input clock,
  
input             r_0_enable,
input      [63:0] r_0_index,
output reg [63:0] r_0_data,
output            r_0_async
,
  
input         w_0_enable,
input  [63:0] w_0_index,
input  [63:0] w_0_data,
input  [63:0] w_0_mask

);
  
`ifdef DISABLE_DIFFTEST_RAM_DPIC
`ifdef PALLADIUM
  initial $ixc_ctrl("tb_import", "$display");
`endif // PALLADIUM

reg [63:0] memory [0 : RAM_SIZE / 8 - 1];

`define MEM_TARGET memory

  string bin_file;
  integer memory_image = 0, n_read = 0, byte_read = 1;
  byte data;
  initial begin
    if ($test$plusargs("workload")) begin
      $value$plusargs("workload=%s", bin_file);
      memory_image = $fopen(bin_file, "rb");
    if (memory_image == 0) begin
      $display("Error: failed to open %s", bin_file);
      $finish;
    end
    foreach (`MEM_TARGET[i]) begin
      if (byte_read == 0) break;
      for (integer j = 0; j < 8; j++) begin
        byte_read = $fread(data, memory_image);
        if (byte_read == 0) break;
        n_read += 1;
        `MEM_TARGET[i][j * 8 +: 8] = data;
      end
    end
    $fclose(memory_image);
    $display("%m: load %d bytes from %s.", n_read, bin_file);
  end
end

`endif // DISABLE_DIFFTEST_RAM_DPIC

  
`ifndef DISABLE_DIFFTEST_RAM_DPIC
import "DPI-C" function longint difftest_ram_read(input longint rIdx);
`endif // DISABLE_DIFFTEST_RAM_DPIC

  
`ifndef DISABLE_DIFFTEST_RAM_DPIC
import "DPI-C" function void difftest_ram_write
(
  input  longint index,
  input  longint data,
  input  longint mask
);
`endif // DISABLE_DIFFTEST_RAM_DPIC

  
`ifdef GSIM
  assign r_0_async = 1'b1;
always @(*) begin
  r_0_data = 0;
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (r_0_enable) begin
    r_0_data = difftest_ram_read(r_0_index);
  end
`else
  if (r_0_enable) begin
    r_0_data = `MEM_TARGET[r_0_index];
  end
`endif // DISABLE_DIFFTEST_RAM_DPIC
end
`else // GSIM
  assign r_0_async = 1'b0;
always @(posedge clock) begin
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (r_0_enable) begin
    r_0_data <= difftest_ram_read(r_0_index);
  end
`else
  if (r_0_enable) begin
    r_0_data <= `MEM_TARGET[r_0_index];
  end
`endif // DISABLE_DIFFTEST_RAM_DPIC
end
`endif // GSIM

  
always @(posedge clock) begin

`ifndef DISABLE_DIFFTEST_RAM_DPIC
if (w_0_enable) begin
  difftest_ram_write(w_0_index, w_0_data, w_0_mask);
end
`else
if (w_0_enable) begin
  `MEM_TARGET[w_0_index] <= (w_0_data & w_0_mask) | (`MEM_TARGET[w_0_index] & ~w_0_mask);
end
`endif // DISABLE_DIFFTEST_RAM_DPIC

end

endmodule
```

**wolf_emit：`build/xs/wolf/wolf_emit/wolf_emit.sv`（Mem1R1WHelper__p12579229824793630810 完整模块）**
```verilog
module Mem1R1WHelper__p12579229824793630810 (
  input wire clock,
  input wire r_0_enable,
  input wire [63:0] r_0_index,
  input wire w_0_enable,
  input wire [63:0] w_0_index,
  input wire [63:0] w_0_data,
  input wire [63:0] w_0_mask,
  output reg [63:0] r_0_data,
  output wire r_0_async
);

  wire signed [63:0] _dpi_ret_0;
  reg signed [63:0] _dpi_ret_0_intm;

  import "DPI-C" function longint difftest_ram_read (
  input longint rIdx
);
  import "DPI-C" function void difftest_ram_write (
  input longint index,
  input longint data,
  input longint mask
);

  assign _dpi_ret_0 = _dpi_ret_0_intm;
  assign r_0_async = 1'b0;

  always @(posedge clock) begin
    if (r_0_enable) begin
      _dpi_ret_0_intm = difftest_ram_read(r_0_index);
    end
    if (w_0_enable) begin
      difftest_ram_write(w_0_index, w_0_data, w_0_mask);
    end
    if (r_0_enable) begin
      r_0_data <= _dpi_ret_0;
    end
  end
endmodule
```

## Bugcase CASE_006（Mem1R1WHelper）
新增一个最小可运行的 bugcase，用于对比 **原始 Mem1R1WHelper** 与 **wolf-sv-parser 处理后版本** 的行为差异。

**目录与文件**：
- `tests/data/xs-bugcase/CASE_006/rtl/Mem1R1WHelper.v`
  - 直接复制自 `build/xs/rtl/rtl/Mem1R1WHelper.v`
- `tests/data/xs-bugcase/CASE_006/rtl/Mem1R1WHelperCase006.sv`
  - wrapper 顶层，仅实例化 1 个 Mem1R1WHelper
- `tests/data/xs-bugcase/CASE_006/tb.v`
- `tests/data/xs-bugcase/CASE_006/tb.cpp`
- `tests/data/xs-bugcase/CASE_006/filelist.f`
- `tests/data/xs-bugcase/CASE_006/Makefile`

**编译参数**（与 `run_xs_diff` 对齐）：
- `+define+DIFFTEST`
- `+define+RANDOMIZE_REG_INIT`
- `+define+RANDOMIZE_MEM_INIT`
- `+define+RANDOMIZE_DELAY=0`
- `+define+RANDOM=32'h0`

**运行方式**：
```
make -C tests/data/xs-bugcase/CASE_006 run
```

**说明**：
- `tb.cpp` 内部为 ref / wolf 各自维护一份 DPI memory，避免交叉污染。
- DPI memory 初始值采用 **基于 index 的可复现 hash**（伪随机）。
- 关键比对：`r_0_data_*` 与 `r_0_async_*`（ref vs wolf）。
- 仅做读取：`w_0_enable_*` 固定为 0，数据完全由 C++ 层初始化（不依赖外部 trace）。
- 若需要验证 `GSIM` 分支是否被错误忽略，可加：
  - `make -C tests/data/xs-bugcase/CASE_006 run EXTRA_VFLAGS=+define+GSIM`

**当前结果**：
- CASE_006 已精简为**不依赖外部 trace**的内置激励，**只读模式**（`w_0_enable=0`）。
- 单个 helper 读取固定序列，DPI memory 采用 **index → hash** 的可复现初始值。
- 当前简化序列下 **ref / wolf 无 mismatch**，作为基础 sanity 检查。

## Difftest RAM trace 插桩（DPIC + RTL）
为定位 `rdata_r_1/2` 的差异，已在 DPIC 与 RTL 两侧加入可选 trace：
- **DPIC 侧**：`difftest_ram_read/write` 直接 `fprintf(stderr, ...)` 输出（`tests/data/xiangshan/difftest/src/test/csrc/common/ram.cpp`）。
- **RTL 侧**：`Mem1R1WHelper` 读写路径加入 `$display/$strobe`（`tests/data/xiangshan/difftest/src/main/scala/common/Mem.scala`），通过 plusarg 打开：
  - 运行 `run_xs_diff` 时可加 `XS_RAM_TRACE=1`（等效传入 `+trace_difftest_ram`）。

注意：trace 非常密集，建议在缩短 `XS_SIM_MAX_CYCLE` 或过滤日志后使用。

## 2026-02-19 DPIC 日志分析（xs_wolf_20260219_212926.log）
本次日志已包含 DPIC 侧 `difftest_ram_read` 输出，结论如下：
- **DPIC 读出的数据与 Gold 一致**：`difftest_ram_read` 打印的数据序列与 `RefillChecker` 的 `Gold:` 8×64b word 完全对应。
- **Core 数据出现“上一条行的 beat”**：例如 `addr=0x80000080` 的 Core beat1/2 变成了上一条 `0x800000c0` 行中 `idx=0x1d/0x1e` 的值；`0x80000040/0x80000000/0x800001c0` 同样出现 lane1/2 滞后一行的现象。

结论：`Refill test failed!` **不是 DPIC 侧内存数据错**，而是 **refill 数据在拼装/对齐/寄存时序上错位**（表现为 lane1/2 滞后一行）。这与此前 `rdata_r_1/2` lane 异常、`rdata_r_0/3` 正常的观测一致。

## 2026-02-19 DPIC vs helper 读 trace 关联（xs_wolf_20260219_212926）
将 `xs_wolf_20260219_212926.log` 中的 `[difftest_ram_read]` 序列与 FST 中 4 个 helper 的读 trace 做关联：
- 依据 `idx % 4` 将 DPIC 读分配到 helper0..3（与 rdata_mem lane 对应）。
- 仅在 `r_0_enable=1` 的 rise 周期取样 helper 的 `r_0_index/r_0_data`。

对比结果（同周期 vs +1 周期）：
- helper0：27 次读，**9 次同周期匹配 + 18 次延迟 1 周期匹配，0 次剩余不匹配**。
- helper3：27 次读，**9 次同周期匹配 + 18 次延迟 1 周期匹配，0 次剩余不匹配**。
- helper1：27 次读，**0 次同周期匹配 + 9 次延迟 1 周期匹配，18 次仍不匹配**。
- helper2：27 次读，**0 次同周期匹配 + 9 次延迟 1 周期匹配，18 次仍不匹配**。

典型样例（helper1）：
- cycle 8260：`r_0_index=0x11`，log 中该 idx 的 data 是 `0x5073300520736509`，但 trace 的 `r_0_data` 仍为 `0x0bd3f0000b53f000`（上一条 `idx=0x1d` 的值）。

结论：**“延迟一拍就能匹配”只对 helper0/3 成立**，helper1/2 的数据不只是延迟一拍，而是混入了上一条 line 的 beat，继续指向 **lane1/2 的拼接/时序问题**。

## 2026-02-19 per-cycle join 复核（joined_per_cycle.csv）
基于 `out/axi4_mem_model_helpers_read_wolf_20260219_212926_joined_per_cycle.csv` 复核，并按 **helper 事件顺序** 将 DPIC 读序列分到 `idx % 4` 对应的 lane 后对齐：
- helper0/3：`match0=9, match1=17, match2=0`。其中 **match0 全部是“相邻两条 DPIC 数据相同”的歧义样本**，更合理解释是 **lag1（延迟 1 条事件）**。
- helper1/2：`match0=0, match1=8, match2=17`，**以 lag2（延迟 2 条事件）为主**。

结论修正：**h0/h3 也主要是延迟 1 条事件，而不是稳定同周期；h1/h2 主要延迟 2 条事件**。

关键片段（事件序列对齐，显示 lag1/lag2）：
- helper0（lag1 样例）：cycle 8258  
  - `r_idx=0x1c r_data=0x06d3f0000653f000`  
  - `dpic[i] data=0x0ad3f0000a53f000`  
  - `dpic[i-1] data=0x06d3f0000653f000`  ← **lag1 命中**
- helper3（lag1 样例）：cycle 8258  
  - `r_idx=0x1f r_data=0x09d3f0000953f000`  
  - `dpic[i] data=0x0dd3f0000d53f000`  
  - `dpic[i-1] data=0x09d3f0000953f000`  ← **lag1 命中**
- helper1（lag2 样例）：cycle 8259  
  - `r_idx=0x1d r_data=0x07d3f0000753f000`  
  - `dpic[i] data=0x0bd3f0000b53f000`  
  - `dpic[i-2] data=0x07d3f0000753f000`  ← **lag2 命中**
- helper2（lag2 样例）：cycle 8259  
  - `r_idx=0x1e r_data=0x08d3f0000853f000`  
  - `dpic[i] data=0x0cd3f0000c53f000`  
  - `dpic[i-2] data=0x08d3f0000853f000`  ← **lag2 命中**

因此 h1/h2 不是简单“晚一拍”，而是 **稳定晚两条事件**；h0/h3 则表现为 **稳定晚一条事件**（同周期匹配样本主要来自“连续两条 DPIC 数据相同”的歧义）。

## helper 与 DPIC 之间的潜在问题点
在当前结构中，**helper 端口之后几乎没有额外硬件**，主要只剩两类可能影响读写结果的结构：
1) **Mem1R1WHelper 内部逻辑**
   - `GSIM` / `DISABLE_DIFFTEST_RAM_DPIC` 分支决定异步/同步读路径。
   - wolf_emit 将读写合并为一个 always，可能改变同周期读写顺序与 `r_0_data` 采样。
   - `r_0_async` 的选择路径会影响直接输出还是寄存输出。
2) **DPIC 内存模型（difftest_ram_read/write）**
   - index 取模/越界检查、掩码写入逻辑。
   - 内存模型实现（mmap/dramsim）可能引入延迟或返回值差异。

因此若 helper 端口信号一致仍出现 mismatch，**更可能的根因在 helper 之前**：
- DifftestMem1R1W 的 lane 拼接与 index 计算（`_GEN + {0..3}`）。
- AXI4RAM / rdata_mem 写入路径（mask、beat 对齐、端序）。
- 读 valid gating 与寄存时序（读到上一拍值）。

## helper1/2 mismatch index 的写入回溯
基于 `out/mem1r1whelper_rw_ref_20260218_225315.txt` 的 rise 相位回溯，**对所有 mismatch index 查找此前写入记录**：
- 统计范围：cycle 0 → 8302（trace 覆盖范围）
- 结果：**所有 mismatch index 在该范围内均未发生写入**（`w_0_enable=0` 或 index 不匹配）。

### helper1 mismatch index（无写入记录）
- idx=29（cycle 8258, 8259）
- idx=21（cycle 8261, 8262）
- idx=13（cycle 8264, 8265）
- idx=5  （cycle 8267, 8268）
- idx=61（cycle 8270, 8271）
- idx=53（cycle 8273, 8274）
- idx=45（cycle 8276, 8277）
- idx=37（cycle 8279, 8280）
- idx=69（cycle 8282, 8283）

### helper2 mismatch index（无写入记录）
- idx=30（cycle 8258, 8259）
- idx=22（cycle 8261, 8262）
- idx=14（cycle 8264, 8265）
- idx=6  （cycle 8267, 8268）
- idx=62（cycle 8270, 8271）
- idx=54（cycle 8273, 8274）
- idx=46（cycle 8276, 8277）
- idx=38（cycle 8279, 8280）
- idx=70（cycle 8282, 8283）

**结论**：这些 mismatch 更可能来自 **trace 覆盖范围之外的历史写入** 或 **其他端口/通路的写入**，而非该 helper 在当前窗口内的写操作。

## Verilator 生成 C++：mem helper 调度差异（确认）
在 `build/xs/wolf/verilator-compile` 的 Verilator 输出中，4 个 `rdata_mem` helper 的 DPI 读/写被拆分到不同的 NBA mtask，导致 **helper0/3 的读数据在本周期被采样，而 helper1/2 的读数据滞后到更晚的 mtask**，形成行为不一致。

### 关键证据（C++ 位置）
**helper0/3 的 DPI 读写在 mtask8 内执行（TOP__219）：**
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_hd5918264__81.cpp:2924`  
  `helper_0` 的 `difftest_ram_read_TOP(...)`
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_hd5918264__81.cpp:2987`  
  `helper_3` 的 `difftest_ram_read_TOP(...)`

**helper1/2 的 DPI 读写在另一个 NBA（mtask45, TOP__1027）里执行：**
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_hd5918264__174.cpp:7230`  
  `helper_1` 的 `difftest_ram_read_TOP(...)`
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_hd5918264__174.cpp:7298`  
  `helper_2` 的 `difftest_ram_read_TOP(...)`

**rdata_mem 的中间采样发生在 mtask8（TOP__222）：**
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_hd5918264__84.cpp:3507`  
  `_helper_[0..3]_r_0_data = helper_[0..3]__DOT___dpi_ret_0_intm`

**最终 rdata_mem.r/r_1/r_2/r_3 也在 mtask8 更新：**
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_hd5918264__81.cpp:3054`  
  `r/r_1/r_2/r_3 = _helper_[0..3]_r_0_data`

### 调度顺序（同一线程 t0）
在 `VSimTop___024root____Vthread__nba__s0__t0` 中，mtask8 先执行，mtask45 之后才执行：
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_h278d43d2__1.cpp:4347`（mtask8）
- `build/xs/wolf/verilator-compile/VSimTop___024root__DepSet_h278d43d2__1.cpp:4381`（mtask45）

### 结论
- helper0/3 的 DPI 读返回值在本周期就被 `_helper_*_r_0_data` 采样并用于 `r_0/r_3`。
- helper1/2 的 DPI 读在更晚的 mtask 执行，但 `_helper_1/2_r_0_data` 已经在 mtask8 被锁存，因此 `r_1/r_2` 使用的是旧值。
- **这就是 4 个 mem helper 行为不一致的 Verilator 侧调度差异根因。**

## emit 报错模块定位（jtag_tick）
当 emit 报错 `kDpicCall return drives multiple state elements: dpic=jtag_tick` 时，对应的 SV 模块为：
- **模块名**：`SimJTAG__p2299961199357505238`
- **来源**：`tests/data/xiangshan/difftest/src/test/vsrc/common/SimJTAG.v`
- **wolf_emit 位置**：`build/xs/wolf/wolf_emit/wolf_emit.sv:96172`（`jtag_tick` 调用与 `__jtag_*`/`__exit` 寄存器集中在此模块）

## 修复方案计划（emit: kDPICall）
结合 GRH 结构与当前 Verilator 行为，拟在 **emit 层面**修复 kDPICall 的语义发散问题，核心目标是：**DPI 读调用必须作为 RHS 表达式就地内联，不引入寄存中间值**，避免被拆进不同 mtask 产生额外延迟。

### 约束（corner case）
**限制：kDPICall 的每个 result value 只能有一个“有状态元素”消费者**。  
若某个 result value 最终落到多个有状态元素，将引入“单次调用 vs 多次调用”的语义歧义；在 emit 层应视为不合法并报错/拒绝生成。

**仅允许“返回值型 + 纯 input 参数”的内联**：  
存在 out/inout 参数或多返回结果时，禁止内联（否则会引入不可控的时序/状态副作用）。

**时序约束必须一致**：  
- kRegister/kMemoryWritePort：kDPICall 的 updateCond / eventEdge 必须与目标 sink 匹配。  
- kLatch/kMemoryReadPort：kDPICall 必须是组合调用（eventEdge 为空），updateCond 必须为常量 1。

### 计划步骤
1) **先定位 kDPICall 驱动的“有状态元素”**  
   - 在 emit 中沿着 kDPICall 的结果 value 追踪，找出其最终落到的 **kRegister / kLatch / kMemoryReadPort / kMemoryWritePort / 其他有状态元素**。  
   - 只对这些“被 kDPICall 驱动的有状态元素”应用特殊处理。

2) **收紧内联条件并建立映射**  
   - 对每个 result value：检查“单一有状态元素驱动”。  
   - 对返回值型 kDPICall：再检查“仅 input 参数 + updateCond/eventEdge 匹配”。  
   - 不满足任一条件时拒绝内联（并报错/保留原语义路径）。

3) **对被驱动的有状态元素单独处理（RHS 内联）**  
   - 对每个被 kDPICall 驱动的有状态元素，**内联其完整 d 表达式**，保证 kDPICall 不被拆成中间寄存/临时信号。  
   - 若 d 表达式是组合树（如 `a <= 1 + dpi_func(...)`），**整棵 RHS 需被内联**。

4) **写型 DPI 维持语句，不引入中间信号**
   - 对 **无返回值 DPI**：保持 `if (en) dpi_task(...);`，避免通过中间信号再驱动，减少被拆分到多个 always/event 的风险。

5) **验证与回归**
   - 生成新的 Verilog/Verilator C++，确认返回值型 DPI 只在目标状态元素的 RHS 内联出现，且不再拆分到不同 mtask。  
   - 用最小复现用例回归，确认 helper 行为对齐，不再出现稳定 lag。

### 验收标准（目标行为）
- 所有 DPI read 均以内联 RHS 形式出现，**不生成 `_dpi_ret_*` 的寄存型中间值**。
- Verilator 输出中，helper0/1/2/3 的 DPI read 调用不再被拆分到不同 mtask。
- 复现用例中 helper1/2 不再出现稳定 lag 现象，读数据对齐策略与 helper0/3 一致。
