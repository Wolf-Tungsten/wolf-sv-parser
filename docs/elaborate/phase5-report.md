# Elaborate 阶段5 完成报告

## Objective 进展
- Elaborate 现已在模块级别解析 slang AST 中的 `net/reg/logic` 声明，生成独立的 net/reg 备忘录（memo），供后续 pass 直接 peek。
- 为所有候选信号收集驱动来源（连续赋值、`always_*`/`initial` 等），根据时序特性自动分类为 net/reg，冲突时立即发出诊断并从 memo 中剔除。
- 构建了针对 memo 功能的 SystemVerilog 用例与 C++ 测试，验证宽度、符号位属性以及冲突检测逻辑，完成 KR5 的 peek 检查。

## KR1 备忘录数据结构
- 新增 `SignalMemoEntry` 数据结构，记录 `ValueSymbol*`、`Type*`、展开后位宽、符号位标记；便于后续直接使用 AST 元信息，无需重复计算。
- `Elaborate` 内部维护 `netMemo_/regMemo_` 两个 `unordered_map<InstanceBodySymbol*, vector<SignalMemoEntry>>`，以模块体 (`InstanceBodySymbol`) 为 key，确保 canonical body 与实例 body 可复用缓存。
- 提供 `peekNetMemo` / `peekRegMemo` 公共接口，返回 `std::span<const SignalMemoEntry>`，测试与后续 pass 均可零拷贝读取。

## KR2 声明遍历与候选收集
- 在 `collectSignalMemos` 中遍历模块成员列表，对 `NetSymbol`、`VariableSymbol`（含 logic）进行筛选，仅保留固定位宽的 bitstream 信号。
- 使用 `TypeHelper::analyze` 计算位宽/符号属性与字段扁平化信息，保证 memo 与端口/Value 建模保持一致。
- 所有候选以 `unordered_map<ValueSymbol*, SignalMemoEntry>` 暂存，确保后续驱动分类仅触及实际声明过的符号。

## KR3 驱动判定算法
- 连续赋值：通过 `ContinuousAssignSymbol::getAssignment()` 获取 `AssignmentExpression`，递归下钻到左值（支持 `concatenate / replicate / streaming concat`、`bit/part select`、`struct` 成员），提取底层 `ValueSymbol`。
- 过程赋值：为模块体构建 `AssignmentCollector`（继承 `ASTVisitor`），覆写 `ExpressionStatement` 与 `ProceduralAssignStatement` 的处理逻辑，捕获 `AssignmentExpression` 左值。
- 时序分类：
  - `always_comb` / `always @(*)` → net；
  - `always_ff`、含 `posedge/negedge` 的 `always`、`initial`/`final`、`always_latch` → reg；
  - `always @(implicit)` → net；
  - `containsEdgeSensitiveEvent` 递归解析 `TimingControl`（`SignalEvent/EventList/RepeatedEvent`）判断是否包含边沿事件。
- 使用 `MemoDriverKind` 位掩码 (Net/Reg) 记录每个符号的驱动集合，若同一符号同时出现 net/reg 驱动，立即下发 `NYI` 诊断并在最终 memo 中剔除该信号。

## KR4 Memo 写入与排序
- 将仅由 net 驱动的候选写入 net memo，仅由 reg 驱动的候选写入 reg memo；其他情况（未驱动或冲突）保持忽略。
- Memo 在写入前按符号名排序，保证 peek/测试得到稳定顺序，便于后续 diff 与序列化。
- 在 `convertInstanceBody` 中于端口建模后调用 `collectSignalMemos`，确保 memo 始终与最新语义内容同步，并与 canonical body 的缓存键一致。

## KR5 测试样例与 peek 验证
- 新增 `tests/data/elaborate/signal_memo.sv`，覆盖：
  - 连续赋值 net (`assign`)；
  - `always_comb`、`always @(*)`、`always_ff`、`always@(posedge/negedge)` 等多种过程块；
  - 同一信号被 net/reg 双重驱动的冲突场景。
- 新测 `tests/elaborate/test_elaborate_signal_memo.cpp`：
  - 通过 `Elaborate::peekNetMemo/peekRegMemo` 验证 `w_assign`、`comb_bus`（宽度 8、signed）、`star_assign` 属于 net memo；
  - 验证 `seq_logic`、`reg_ff`、`latch_target` 属于 reg memo；
  - 确保 `conflict_signal` 不出现在任一 memo 且诊断列表包含冲突提示。
- CMake 新增 `elaborate-signal-memo` 可执行与 `ctest` 目标，自动编译/运行上述验证。

## 后续注意事项
- 目前只识别 `net/variable` 声明，尚未覆盖 interface/modport 等更复杂声明形态；若后续阶段需要，需要在候选收集阶段扩展。
- 过程块分类基于 `TimingControl` 是否含显式边沿，尚未处理复杂嵌套（如 `wait`、`disable`）引发的隐式时序；若上游语义要求更严格，需要进一步分析 `Statement` 树。
- memo 暂未序列化到 JSON artifacts，仅供 C++ API peek；若后续需要跨进程复用，可考虑在 GRH artifacts 中附带镜像信息。
