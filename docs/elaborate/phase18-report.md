# 阶段18：SeqAlwaysConverter 处理 memory 读写口

## 完成内容
- **同步读口（KR1）**：`SeqAlwaysRHSConverter::convertElementSelect` 识别指向 `reg memo` 中 `kMemory` 条目的 `mem[addr]` 读取，调用 `SeqAlwaysConverter::buildMemorySyncRead` 生成 `kMemorySyncReadPort`，并自动绑定块级时钟、地址与常量 1 的 enable，输出 Value 直接回填给 RHS 计算链路。
- **整字写入（KR2）**：`SeqAlwaysLHSConverter::convert` 在解析 LHS 时优先检测 `mem[...] <= rhs`，绕过通用 write-back 流程，立即把地址/RHS Value 记录到 `memoryWrites_` 列表；Finalize 阶段基于 `memoryRowWidth`（从 `kMemory` attribute 读取）创建 `kMemoryWritePort`，顺序保持源代码一致。
- **按位写掩码（KR3）**：当 LHS 匹配 `mem[addr][bit_index] <= bit_value` 时，记录为 `memoryBitWrites_`，Finalize 使用 `buildShiftedBitValue`/`buildShiftedMask` 生成位移后的 data/mask，再创建 `kMemoryMaskWritePort`（clk/addr/en/data/mask 五个 operand 全部接好）。若同一地址存在多条 bit 写入，记录顺序即生成顺序，方便后续冲突处理。
- **公共辅助（KR1~KR3 支撑）**：`SeqAlwaysConverter` 新增 clock/en 缓存、内存 helper op/value 命名器、row width 推导、常量 enable 复用等工具，确保多端口不会命名冲突；同时保留 `WriteBackMemo` 给寄存器使用，memory 写入完全独立。
- **回归测试（KR4）**：`tests/data/elaborate/seq_always.sv` 添加 `seq_stage18`，包含同步读 + 整字写 + 单 bit mask 写；`tests/elaborate/test_elaborate_seq_always.cpp` 新增针对 `seq_stage18` 的断言，检查 sync read 结果驱动 `rd_reg`、write/mask 端口的 clk/addr/en/data/mask 连接及位移操作。`ctest -R elaborate-seq-always` 成功验证整套流程。

## 核心实现要点
- **LHS 解析前置**：通过逐层剥离 implicit conversion / member / range / element select，锁定最内层 `mem[...]`，在 SeqAlways 路径下直接返回，避免 write-back memo 报 “Element select index must be constant”。
- **Finalize 分层**：`planSequentialFinalize` 先消费 write-back memo 里的寄存器条目，再依次落地 memory word/mask 写端口，保证不同设备互不干扰。
- **按位写建模**：数据与 mask 均使用 `kShl` + 常量/拼接构造，bit_index 作为 shift operand，可兼容动态索引；若 RHS 不是单 bit 或 memory 条目缺失 metadata，会通过 `reportMemoryIssue` 抛出诊断。
- **宽度一致性**：`memoryRowWidth` 优先读取 `kMemory` attribute，避免 mem 是 unpacked array 时仅依赖 `SignalMemoEntry::width` 导致 8×16 这类结构的位宽错误。

## 测试记录
- `cmake --build build --target elaborate-seq-always`
- `cd build && ctest -R elaborate-seq-always --output-on-failure`

## 后续建议
1. **多端口冲突检测**：当前允许同一 always 块生成任意数量的 write/mask 端口，可在记录阶段增加检查（同一地址的重复整字写、mask 与整字的混合顺序）并输出更友好的告警。
2. **掩码聚合**：可以为同一 `addr` 的多个 bit 写入在 finalize 时合并成单个 mask 端口，减少图规模，同时开辟合成字节/半字写的路径。
3. **Enable 推断增强**：目前默认 `en=1`，后续可在 AST 中追踪 `if (enable) mem[...] <= ...;` 结构以复用真实 enable 信号，并在测试中覆盖 gated write/read。
