# Elaborate 阶段11 完成报告

## Objective 进展
- 将 `ContinuousAssignSymbol` 接入 Elaborate：每条 `assign` 语句都会复用 `CombRHSConverter` 生成 RHS `Value`，并交由新增的 `ContinuousAssignConverter` 解析 LHS，最终通过 `WriteBackMemo` 记录「连续赋值」写意图（src/elaborate.cpp:1885-1990, 2376-2405, 2447-2564）。
- 扩展 `WriteBackMemo::recordWrite` 以同一 `SignalMemoEntry` 为 key 聚合切片，允许多个 assign 共同驱动同一 net，同时复用阶段10的 `composeSlices/attachToTarget` 逻辑（src/elaborate.cpp:612-632）。
- `ContinuousAssignConverter` 覆盖标量、结构体、packed/unpacked array、concat/range select 等 LHS 形态：借助 TypeHelper flatten 后的路径信息推导位段，再按需生成 `kSliceStatic`/`kConcat` 调整 RHS 位宽（src/elaborate.cpp:449-749）。
- RHS/LHS 转换器均使用全局自增的 `instanceId` 参与命名，避免 `range_slice_*` / `_assign_slice_*` 依赖对象地址导致的不稳定符号，同时保证多实例下 Graph 内符号唯一。

## 数据结构与算法
- **ContinuousAssignConverter**：递归解析 LHS 表达式，先处理 concat，再根据表达式构造 TypeHelper 路径（`foo.bar[3]` 等）查找 flatten 位段；遇到 `RangeSelect` 会借助常量求值拆解成 bit 级路径。每个 net 先在内存里累加 `Slice`，统一调用 `WriteBackMemo::recordWrite`，这样阶段10的排序/补零可直接复用（src/elaborate.cpp:449-749）。
- **RHS/LHS Converter 唯一命名**：为每个转换器实例分配自增 `instanceId`，`makeValueName/makeOperationName` 与 LHS 的切片命名均将 `instanceId + 局部计数` 编入 symbol，既保证符号唯一，又避免 ASLR 影响导致的非确定性命名。
- **LHS 切片创建**：当 concat 拆分 RHS 时，如果子表达式宽度小于父宽度，会生成 `kSliceStatic`，并自动附带零填充（通过 `composeSlices`）覆盖未写位段，确保 net 始终以 SSA 形式被单一 `kAssign` 驱动（src/elaborate.cpp:676-712, 704-858）。

## 测试与验证
- 新增 `tests/elaborate/test_elaborate_assign.cpp`，配合 `tests/data/elaborate/continuous_assign.sv` 构造 scalar、struct、array、partial、concat 等 assign 场景：
  - 验证 `scalar_net` 直接连接输入端口，`struct_net` 由三个切片汇聚成 `kConcat`，`array_net`/`partial_net` 自动插入零填补洞，`concat_b` 仅驱动低 2 bit 而高位零填。
  - 测试输出同时写出 JSON artifact，便于人工审查（CMakeLists.txt:142-166, tests/...）。
- 保持 `elaborate-write-back-memo` 回归通过，确认 write-back 改动未破坏阶段10 的单元测试。

## 后续工作建议
1. Continuous assign 目前仅覆盖 combinational net；若未来要支持 `wand`、`wor` 等多驱动解析，需要在 `WriteBackMemo` 层面引入冲突检测/优先级策略。
2. 明确记录并测试连续赋值 LHS 只能使用常量 range/index（符合综合语义），若遇到动态索引需求应转交过程块或上层结构，而不是扩展 GRH 支持。
3. 现阶段 `CombRHSConverter` 在每条 assign 中重新实例化，后续可缓存于模块级别以复用表达式缓存/常量求值上下文。
