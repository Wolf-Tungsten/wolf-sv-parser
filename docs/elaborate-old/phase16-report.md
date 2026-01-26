# 阶段16：SeqAlwaysConverter 框架落地

## 完成内容
- **流程接入（KR1）**：`Elaborate::processSeqAlways` 通过 `isSeqProceduralBlock` 判定含沿触发或 `always_ff` 的过程块，构造 `SeqAlwaysConverter` 并在 `convertInstanceBody` 中与 comb/shared流程并列执行。`collectSignalMemos` 也会记录寄存器唯一的 `drivingBlock`，为时序 elaboration 提供上下文。
- **AlwaysConverter 基类（KR2）**：抽取 `AlwaysConverter` 作为 comb / seq 共享的控制器，内含 shadow 写集合、loop scope、条件/循环访问器以及 RHS/LHS converter 工厂。`CombAlwaysConverter` 与 `SeqAlwaysConverter` 仅覆写模式标签与赋值许可策略，代码结构保持一致。
- **非阻塞语义（KR3）**：新建 `SeqAlwaysLHSConverter`、`SeqAlwaysRHSConverter`，配合 `AlwaysConverter::handleAssignment` 只允许 NBA，阻塞赋值会即时报告；块内写集合通过 shadow memo 记录，并在 `flushProceduralWrites` 阶段写入 `WriteBackMemo::AssignmentKind::Procedural`，确保同一时钟沿的多次写入按源顺序合并。
- **Finalize 规划（KR4）**：虽然阶段16尚未真正连接 kRegister，但 `planSequentialFinalize()` 已插入 TODO 诊断，提醒评审在阶段17前需要将寄存器/存储器绑定落地。诊断记录了目标寄存器名称与类别（register vs memory），便于后续跟踪。

## 核心实现要点
- **ShadowState/Frame**：与 comb 共用的 shadow map 实现在 seq 模式下同样适用，可在块内即时重写 net/reg，并在 flush 时一次性写回 write-back memo，维持 SSA 语义。
- **Loop 与控制流保留**：虽然 seq 尚未支持 if/case/loop 的完整语义，但基类访问器已经复用 comb 的基础设施（`visitConditional`, `visitForLoop` 等），并通过 `reportControlFlowTodo` 提示尚未覆盖的语句种类。
- **驱动信息传播**：reg memo 中的 `drivingBlock` 在 `ensureRegState` 中被用来创建 `kRegister`、设置 `clkPolarity` 属性，同时确保每个寄存器只被一个时序块写入，否则立刻诊断冲突。

## 测试记录
- `cmake --build build`
- `cd build && ctest --output-on-failure -R elaborate-signal-memo`（验证 reg memo / kRegister 生成）
- `ctest -R elaborate-smoke`（确保 seq 块接入后整体流程仍可执行并输出 TODO 诊断）

## 后续建议
1. **控制流实现**：利用基类已有的 shadow/merge 基础，为 seq 块补充 if/case/loop，确保块内存在条件分支时写集合仍保持确定性。
2. **复位/存储器支持**：阶段17仅覆盖 kRegister 数据绑定，还需规划 `kRegisterRst`/`kRegisterArst` 与 `kMemory` 读写口的接入策略，以便处理含复位、内存写口的 always_ff。
3. **诊断精细化**：当前 TODO 信息较为粗粒度，可扩展为输出源文件位置、寄存器名称与尚未连线的 operand 类型，降低后续调试成本。
