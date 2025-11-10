# LHSConverter 深入解析

左值（LHS）解析在 elaborate 流程中承担着“把 SystemVerilog 赋值表达式映射到 GRH 写回语义”的关键角色。阶段12将相关逻辑抽象为 `LHSConverter` 体系，使连续赋值与 `always_comb` 等过程块可以共享同一套切片管线，同时根据不同语义采用各自的写回策略。

本文介绍该体系的设计目的、数据流转、派生类、以及如何在新场景中使用它。

---

## 1. 为什么需要 LHSConverter？

GRH 要求所有 net/reg 的写入满足 SSA（Single Static Assignment）约束，但 SystemVerilog 的 LHS 可以是任意组合：

- 结构体/数组字段：`struct_net.lo[1:0] = ...;`
- 合并表达式：`{a[3:0], b[1:0]} = ...;`
- 层级切片、成员访问嵌套

早期实现里，连续赋值和 `always_comb` 分别持有独立的 LHS 解析逻辑，导致：

1. 代码重复：字段路径推导、`kSliceStatic` 构造、memo 匹配等步骤在多个类中重复出现。
2. 难以扩展：若要支持新的过程块，必须复制整套逻辑并维护多份 bug 修复。
3. 耦合严重：continuous assign 的类名和行为与“连续赋值”强绑定，无法在 procedural 场景下重用。

为解决这些问题，阶段12将 LHS 解析抽象到 `LHSConverter` 基类，再根据不同写回策略派生扩展。

---

## 2. 架构概览

```
AssignmentExpression (lhs) ──> LHSConverter::lower(...)
                                    │
                                    ├─ validate width
                                    ├─ walk expression tree (concat/member/array...)
                                    ├─ produce WriteResult{target entry, slices[]}
                                    ▼
                        ContinuousAssignLHSConverter
                        CombAlwaysLHSConverter
                        (未来的其他派生类)
```

### 2.1 基类 `LHSConverter`

| 成员/方法 | 说明 |
|-----------|------|
| `Context` | 注入当前 `grh::Graph`、net memo (`std::span<const SignalMemoEntry>`)、origin symbol 以及 diagnostics。|
| `lower(lhsExpr, rhsValue, outResults)` | 核心入口。确保 LHS 宽度与 RHS 匹配，递归解析并填充 `outResults`（每个元素包含目标 memo entry + 有序切片列表）。|
| `handleConcatenation/handleLeaf` | 处理不同类型的 LHS 节点，必要时构造 `kSliceStatic` value。|
| `buildFieldPath/resolveBitRange` | 借助 `SignalMemoEntry::fields` 将结构体/数组路径映射到扁平 bit 范围。|
| `allowReplication()` | 钩子，默认禁止 replication/streaming concat；派生类可放开。|

### 2.2 WriteResult / Slice
- `WriteResult.target`：指向某个 `SignalMemoEntry`（net memo 中的某条 net/logic）。
- `WriteResult.slices`：按 MSB→LSB 排好的 `WriteBackMemo::Slice` 数组，每个切片包含 `[msb, lsb]`、RHS `grh::Value*` 以及源 AST 指针，便于后续写回生成 concat/assign。

---

## 3. 已有派生类

### 3.1 ContinuousAssignLHSConverter

- 场景：`assign lhs = rhs;`
- 流程：
  1. 调用 `lower` 获取 `WriteResult`。
  2. 将结果以 `AssignmentKind::Continuous` 写入当前模块的 `WriteBackMemo`。
  3. write-back finalize 时会根据记录生成 `kAssign` / `kConcat` 等 GRH operation，保持阶段10约定的 SSA 语义。

### 3.2 CombAlwaysLHSConverter

- 场景：`always_comb` / `always @(*)` 过程块中的阻塞赋值。
- 流程：
  1. `lower` 产生 `WriteResult`。
  2. 将切片交给 `CombAlwaysConverter::handleEntryWrite`，写入 block-level shadow map，而非立即写回 memo。
  3. `CombAlwaysConverter` 在每次 RHS 读取时优先查询 shadow，确保阻塞赋值“写后即读”语义。
  4. 过程块遍历完成后，通过 `memo_.recordWrite(... AssignmentKind::Procedural ...)` 将 shadow 内容落入 `WriteBackMemo`，统一生成最终操作。

---

## 4. 数据流细节

1. **位宽校验**：`lower` 在进入递归前校验 `lhsWidth == rhsValue.width()`，不满足时直接报错。
2. **字段路径解析**：
   - 以 `buildFieldPath` 透过 `SignalMemoEntry::fields` 还原形如 `struct_net.lo[1].field` 的扁平路径。
   - `resolveBitRange` 决定 `[msb:lsb]`，并根据 `RangeSelect` / `ElementSelect`/成员访问进行组合。
3. **切片构建**：
   - 若 RHS 需要截取子区间，使用 `createSliceValue` 生成 `kSliceStatic` operation，并将 `grh::Value*` 附到切片。
4. **结果输出**：
   - 所有切片暂存于 `pending_[entry]`，待 LHS 树遍历完毕后在 `flushPending` 中整理为 `WriteResult` 列表，交给派生类处理。

---

## 5. 如何在新场景中复用？

假设要支持新的 procedural 块（例如 `always_latch`），步骤如下：

1. **准备上下文**  
   ```cpp
   LHSConverter::Context ctx{
       .graph = currentGraph,
       .netMemo = netMemoSpan,
       .origin = &blockSymbol,
       .diagnostics = diagnostics};
   ```

2. **实现派生类**  
   - 继承 `LHSConverter`，根据需求覆写 `allowReplication()` 或添加额外校验。
   - 在 `convert` 中调用 `lower`，拿到 `std::vector<WriteResult>` 并按场景决定如何写回（直接写入 memo、写 shadow、或排队到某个队列）。

3. **接入流程**  
   - 在对应的 elaboration pass 中实例化派生类，并在解析每条赋值语句时调用 `convert`。

这样即可在保持 LHS 解析一致性的前提下，自由扩展写回策略。

---

## 6. FAQ

> **Q：LHSConverter 是否处理非组合类型（如 `reg`）？**  
> A：它只关注“表达式如何映射到 memo entry 的 bit 范围”，不区分 net/reg。只要目标符号已出现在 net memo（或后续扩展为 reg memo），就能生成切片。真正的数据附着（写入 `kAssign` 或 `kRegister` operand）由 write-back memo finalize 来完成。

> **Q：如果 LHS 包含 streaming concat / replication 怎么办？**  
> A：默认 `allowReplication()` 返回 `false`，基类会报错。若某派生类需要支持，可以覆写该钩子并自行扩展 `processLhs` 逻辑。

> **Q：为什么需要 origin symbol？**  
> A：用于诊断定位，且 `EvalContext` 计算常量选择时需要根符号。若传入空指针，`ensureEvalContext()` 会触发断言，有助于尽早发现调用层漏传。

---

通过 LHSConverter 这一体系，Elaborate 的 LHS 解析具备了：

- **统一代码路径**：所有赋值语句都使用同一套 flatten/切片逻辑，大幅减少重复实现。
- **灵活写回策略**：派生类可以与特定语义（Continuous, Procedural, Future Seq 等）绑定，无需复制底层解析代码。
- **可持续扩展**：随着阶段推进（例如支持 `always_ff`、非阻塞赋值、memory write-back），只需追加派生类和策略层逻辑即可，不会破坏现有功能。

希望本文档能帮助贡献者快速理解并运用 LHSConverter，在不同 elaboration 场景中获得一致而可靠的 LHS 处理能力。***
