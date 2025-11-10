# LHSConverter 说明

`LHSConverter` 是阶段12新增的公共基类，用于解析 SystemVerilog 赋值语句的左值表达式，并将其映射到 GRH 写回所需的 `WriteBackMemo::Slice` 列表，以便不同场景（连续 `assign`、`always_comb` 等）共享同一套切片/路径逻辑。

## 类结构
- **LHSConverter**（基类）
  - 构造参数 `Context`：包含当前 `grh::Graph`、net memo（`std::span<const SignalMemoEntry>`）、origin `Symbol` 以及诊断接口。
  - `lower(AssignmentExpression, Value&)`：校验 LHS 宽度、递归解析 concat / member / array / range 结构，必要时创建 `kSliceStatic`。所有叶子会写入 `pending_`，最终 `flushPending` 输出若干 `WriteResult`（每个对应一个目标 memo entry + 一组 slices）。
  - 依赖 `SignalMemoEntry::fields` 的 flatten 元数据以复原 `[msb:lsb]`。
  - 提供可覆写钩子 `allowReplication()`，默认禁止 LHS 出现 replication/streaming concat（可由后续派生类开启）。

- **ContinuousAssignLHSConverter**
  - 场景：阶段11的 `assign lhs = rhs`。
  - `convert` 调用基类 `lower` 后，将每个 `WriteResult` 以 `AssignmentKind::Continuous` 写入 `WriteBackMemo`，保持阶段10引入的 SSA 写回流程。

- **CombAlwaysLHSConverter**
  - 场景：阶段12的 `always_comb` / `always @(*)`。
  - `convert` 同样调用基类 `lower`，但输出切片并不会立即写入 memo，而是交给 `CombAlwaysConverter::handleEntryWrite`。后者维护 block 级 shadow map，确保阻塞赋值语义（“写在后、读在先”），并在过程块结束后统一把 shadow 中的切片落入 `AssignmentKind::Procedural`。

## 使用方式
1. 实例化 `LHSConverter::Context`（graph、net memo、origin symbol、diagnostics）。
2. 针对具体场景选择派生类（例如 assign 使用 `ContinuousAssignLHSConverter`，always_comb 使用 `CombAlwaysLHSConverter`）。
3. 调用 `convert(assignment, rhsValue)`，失败时可查看 diagnostics。成功时，派生类自动将结果写入对应的 memo 或 block shadow。

## 设计收益
- **去耦**：原 `ContinuousAssignConverter` 中的 LHS 解析逻辑不再与特定写回策略绑死，后续实现 `always_ff`、`initial` 等 procedural 驱动时，只需新增 LHSConverter 派生类即可。
- **一致性**：所有 LHS 解析都复用 TypeHelper flatten 元数据和统一的诊断信息，减少重复代码。
- **扩展性**：基类钩子允许控制哪些语法形式被支持（如 replication/streaming concat），并能在未来轻松添加 mem 写回或层级引用等高级特性。***
