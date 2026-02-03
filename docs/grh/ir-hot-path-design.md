# GRH 热路径 IR 数据结构设计规划

本规划基于 `docs/GRH-representation.md` 的语义定义与当前 GRH 实现的 symbol 化索引方式，聚焦“热路径”场景（大规模电路表达、密集遍历与优化），目标是接口更安全简洁、整体性能更高，同时保持 IR 语义不变、序列化兼容。

## 目标

- 提升热路径性能：减少哈希查找与指针追踪，提高缓存命中率与遍历吞吐。
- 提升接口安全性：避免裸指针与跨 Graph 误用，通过句柄与访问视图约束使用。
- 保持语义与序列化兼容：GRH 语义、JSON 格式不变，仅优化内存结构与访问方式。
- 兼顾可扩展性：为后续优化（pattern match、DCE、CSE 等）提供高效 def-use 关系支持。

## 现状小结（用于对比）

- 以 `std::string` 作为 Value/Operation/Graph 的主索引。
- Graph 内部以 `unordered_map<symbol, unique_ptr<T>>` 存储，并维护插入顺序向量。
- Operation/Value 关系通过 symbol 列表 + 指针缓存维持。

当前结构的主要瓶颈：

- symbol/string 与哈希表查找频繁进入热路径。
- Value/Operation 访问需要多层 indirection，局部性较差。
- def-use 关系以 vector 维护，但需要多次解引用和 symbol 查找。

## 设计方案概览

核心思路：使用紧凑索引句柄 + 连续存储（SoA/arena） + 构建/冻结分离。

### 1) 句柄与符号表

- 引入 `SymbolTable` 进行字符串驻留，产生稳定的 `SymbolId`（uint32_t）。
- `SymbolTable` 可以设计为多层次：
  - Netlist 级 `NetlistSymbolTable`：只管理 Graph/alias 等跨图符号。
  - Graph 级 `GraphSymbolTable`：只管理 Value/Operation/port 等图内符号。
- 这种分层有利于并行性：多个 Graph 可并行构建，避免共享锁争用；Graph 内符号查找也更小、更快。
- 代价是潜在的字符串重复存储；可选用共享字符串池或只在 Netlist 层做全局驻留。
- `SymbolId` 只保证在同一个 `SymbolTable` 生命周期内稳定，不跨进程持久化；跨层不可直接比较。
- 约束：Netlist 层只驻留 Graph 名称；跨图引用（例如 instance 目标）统一走 NetlistSymbolTable，Graph 层不保存跨图符号。
- 典型 API 形态（区分申请与查询）：
  - `SymbolId intern(std::string_view text)`：申请新符号；若已存在则返回 invalid 或抛错。
  - `SymbolId lookup(std::string_view text)`：仅查询，存在则返回 id；缺失返回 invalid。
  - `bool contains(std::string_view text)`：是否已驻留。
  - `std::string_view text(SymbolId id)`：从 id 取回字符串视图。
  - `bool valid(SymbolId id)`：是否为有效符号（例如 `id != 0`）。
  - `SymbolId` 直接用 `==` 比较即可，无需额外 equals。
- 线程模型：
  - 构建期允许写入，需外部串行或内部加锁；
  - 冻结后只读，可无锁访问。
- 内存模型：
  - 使用连续字符串池或 `std::pmr::monotonic_buffer_resource` 降低分配成本；
  - `text(id)` 返回的视图在 `SymbolTable` 生命周期内有效。
- 重命名规则：
  - `SymbolTable` 仅做驻留，不做删除或就地修改；
  - 重命名等价于申请新的 `SymbolId` 并更新持有者的索引映射；
  - `GraphView` 不支持重命名，需在构建期通过 `GraphBuilder` 完成或重新 freeze。
- 引入强类型句柄 `ValueId/OperationId/GraphId`，形如：
  - `struct ValueId { uint32_t index; uint32_t gen; };`
  - 通过 generation 防止悬挂句柄，在 release 模式可仅保留 index。
- 对外 API 以 `ValueId/OperationId` 作为唯一引用，避免裸指针跨图使用；`GraphId` 仅用于 Netlist 容器内部索引。

### 2) Graph 热路径存储布局

Graph 内部采用“结构数组”或“紧凑记录 + 索引”的布局：

- Value 数据：
  - `valueSymbol[ValueId] -> SymbolId`
  - `valueWidth[ValueId] -> int32_t`
  - `valueFlags[ValueId] -> bitset` (signed/input/output)
  - `valueDef[ValueId] -> OperationId` (invalid 表示无定义)
  - `valueUsersRange[ValueId] -> {offset, count}` 指向用户边列表
  - `valueSrcLoc[ValueId] -> DebugInfoIndex` (可选 side table)
- Operation 数据：
  - `opKind[OperationId] -> OperationKind`
  - `opSymbol[OperationId] -> SymbolId`
  - `opOperandsRange[OperationId] -> {offset, count}`
  - `opResultsRange[OperationId] -> {offset, count}`
  - `opAttrsIndex[OperationId] -> AttrTableIndex` (热路径可指向轻量表)
- 连接关系（连续数组）：
  - `operandList[] -> ValueId`
  - `resultList[] -> ValueId`
  - `useList[] -> {OperationId, operandIndex}`

说明：

- `*Range` 结构用 `offset + count` 指向一段连续数组，便于紧凑存储与快速遍历。
- `useList` 由 Graph 统一维护，Value 的 user 信息通过 range 访问。
- `useList` 记录的是 **Value 被哪些 op 以第几个 operand 使用**：每条为 `{OperationId op, uint32_t operandIndex}`；def 单独由 `valueDef` 表示，端口绑定不进入 `useList`。
- 若构建期插入导致 operands 非连续，可在构建期使用每 op 独立的临时向量，`freeze()` 时统一压平为连续数组；热路径只依赖冻结后的连续布局。
- 这里的“连续”指**每个 op 自己的 operands 列表在全局数组里占一段连续区间**；Value 被多个 op 使用不会破坏这一点，因为每个 op 的 operands 区间彼此独立。

### 3) Netlist 与 Graph 管理

- Netlist 保存 Graph 的连续容器（如 vector/arena），以 `GraphId` 或索引访问。
- Graph 的 symbol/alias 仍可通过 `SymbolId -> GraphId` 的 map 解决，但热路径不应依赖。
- Graph 之间引用（instance）仅存 GraphId 或 SymbolId（用于序列化/外部兼容）。

## Graph 缓存策略（细粒度脏标记 + 增量更新）

`Graph` 类维护从 `GraphBuilder` 到只读视图的缓存，以加速 `values()` / `operations()` / `ports()` 等查询。为避免每次修改后的全量重建，采用细粒度脏标记策略：

### 缓存结构

- `valuesCache_`：`vector<ValueId>`，缓存所有 alive value 的 ID
- `operationsCache_`：`vector<OperationId>`，缓存所有 alive operation 的 ID
- `inputPortsCache_` / `outputPortsCache_` / `inoutPortsCache_`：端口缓存

### 脏标记

三个独立的 bool 标记，替代原来的单一 `cacheValid_`：
- `valuesCacheDirty_`：values 缓存需要重建
- `operationsCacheDirty_`：operations 缓存需要重建
- `portsCacheDirty_`：ports 缓存需要重建

### 修改操作的缓存策略

| 操作 | 缓存影响 | 实现方式 |
|------|----------|----------|
| `createValue` | 增量更新 | `valuesCache_.push_back(id)`，不标记 dirty |
| `createOperation` | 增量更新 | `operationsCache_.push_back(id)`，不标记 dirty |
| `eraseValue` | 标记 dirty | `invalidateValuesCache()`，下次查询时重建 |
| `eraseOp` | 标记 dirty | `invalidateOperationsCache()`，下次查询时重建 |
| `bindInput/Output/InoutPort` | 增量更新 | 直接同步对应 ports cache |
| `addOperand`/`replaceOperand`/`replaceAllUses` 等 | 无影响 | 不触发缓存失效 |
| `setAttr`/`setOpKind`/`setSrcLoc` 等 | 无影响 | 不触发缓存失效 |

### 设计 rationale

1. **create 操作增量更新**：`addValue`/`addOp` 只是追加到 builder 的数组末尾，对应的缓存也可以直接追加，无需遍历重建。

2. **erase 操作标记 dirty**：erase 会导致数组中出现空洞（alive=false），从缓存中精确移除特定元素需要 O(N) 扫描和移动，不如直接标记 dirty 让下次查询时统一重建（批量 erase 时尤其如此）。

3. **operand/attribute 修改无影响**：这些修改只影响 operation 的内部状态，不影响 value/operation/port 的列表，因此不应使缓存失效。

4. **细粒度分离**：values/operations/ports 的修改互不影响，避免不必要的级联重建。例如频繁添加 value 但很少查询 operations 时，operations 缓存保持有效。

### 性能验证

实际 c910 convert 性能测试：
- 优化前（STEP 0002 基线）：84s
- 优化后（细粒度脏标记）：**5s**
- **加速比：16.8x**

这表明细粒度缓存策略消除了构建期的绝大多数冗余重建，将 `ensureCaches` 从主要瓶颈降至可忽略水平。

### 与 GraphView 的关系

当 `Graph` 处于 frozen 状态（只有 `view_`，没有 `builder_`）时，缓存机制不生效，直接返回 `GraphView` 的数据。缓存只在 mutable 模式（有 `builder_`）下维护。

## 热路径 API 形态（完整定义）

热路径只读访问通过 `GraphView` 提供；构建与变换通过 `GraphBuilder` 完成。`GraphView` 不允许原地修改，所有变更在 `GraphBuilder` 中维护一致性并在 `freeze()` 时压平为连续布局。

### 基本类型

- `SymbolId`：由 `GraphSymbolTable` 产生的图内符号 id；`invalid` 表示“无符号”（允许 op 缺失 symbol）。
- `ValueId/OperationId/GraphId`：句柄（index + 可选 generation）。
- `Span<T>`：连续只读视图（`data + size`）。
- `Port`：`{ SymbolId name, ValueId value }`。
- `ValueUser`：`{ OperationId op, uint32_t operandIndex }`。
- `AttrKV`：`{ SymbolId key, AttrValue value }`。
- `AttrValue`：`variant<bool, int64_t, double, std::string, vector<bool>, vector<int64_t>, vector<double>, vector<std::string>>`。
- `SrcLoc`：源位置（文件、行、列信息）。

### GraphView（只读，热路径）

- `Span<OperationId> operations()`：按稳定顺序遍历 op。
- `Span<ValueId> values()`：按稳定顺序遍历 value。
- `Span<Port> inputPorts()` / `Span<Port> outputPorts()`：端口按名字排序。
- `OperationKind opKind(OperationId op)`。
- `Span<ValueId> opOperands(OperationId op)`。
- `Span<ValueId> opResults(OperationId op)`。
- `SymbolId opSymbol(OperationId op)`：可能为 invalid，表示未设置符号。
- `Span<AttrKV> opAttrs(OperationId op)`。
- `std::optional<AttrValue> opAttr(OperationId op, SymbolId key)`：按 key 查询单个属性（可线性查找或使用缓存索引）。
- `SymbolId valueSymbol(ValueId v)`。
- `int32_t valueWidth(ValueId v)`，`bool valueSigned(ValueId v)`。
- `bool valueIsInput(ValueId v)` / `bool valueIsOutput(ValueId v)`。
- `OperationId valueDef(ValueId v)`：无定义时返回 invalid。
- `Span<ValueUser> valueUsers(ValueId v)`。
- `std::optional<SrcLoc> valueSrcLoc(ValueId v)`。
- `std::string_view symbolText(SymbolId id)`：只读符号文本视图。

一致性保证：

- `Span` 在 `GraphView` 生命周期内稳定。
- `GraphView` 是不可变快照，不承担增量更新职责。
- 构建期的修改不会反映到已有 `GraphView`；如需递归或多轮基于最新状态的遍历，需手动 `freeze()` 生成新视图。

### GraphBuilder（构建期，非热路径）

- `ValueId addValue(SymbolId sym, int32_t width, bool isSigned)`。
- `OperationId addOp(OperationKind kind, SymbolId sym)`：`sym` 可为 invalid，表示不设置符号。
- `void addOperand(OperationId op, ValueId v)` / `void addResult(OperationId op, ValueId v)`。
- `void replaceOperand(OperationId op, size_t idx, ValueId v)`。
- `void replaceResult(OperationId op, size_t idx, ValueId v)`。
- `bool eraseOperand(OperationId op, size_t idx)`：移除指定 operand，更新 useList，并重新编号该 op 之后的 operandIndex。
- `bool eraseResult(OperationId op, size_t idx)`：仅当该 result 无 users 且非端口时允许；清除 valueDef 并从结果列表移除。
- `void setOperands(OperationId op, Span<ValueId> ops)` / `void setResults(OperationId op, Span<ValueId> results)`：重建该 op 的输入/输出列表并重算 useList/valueDef。
- `void replaceAllUses(ValueId from, ValueId to)`。
- `bool eraseOp(OperationId op)`：移除 op，并在构建期更新 operands 的 useList；若其结果仍被使用则失败。
- `bool eraseOp(OperationId op, Span<ValueId> replacementResults)`：允许删除仍被使用的 op，将其每个 result 的所有 uses 替换为对应 replacement（数量需一致）。
- `bool eraseValue(ValueId v)`：仅当无 users、非端口、且不再作为 op result 时允许；否则失败。
- `void bindInputPort(SymbolId name, ValueId v)` / `void bindOutputPort(SymbolId name, ValueId v)`。
- `void setAttr(OperationId op, SymbolId key, AttrValue value)`。
- `bool eraseAttr(OperationId op, SymbolId key)`。
- `void setValueSrcLoc(ValueId v, SrcLoc loc)`。
- `void setOpSymbol(OperationId op, SymbolId sym)` / `void setValueSymbol(ValueId v, SymbolId sym)`：`sym` 不允许为 invalid。
- `void clearOpSymbol(OperationId op)` / `void clearValueSymbol(ValueId v)`。
- `GraphView freeze()`：压平 operands/results/useList，构建 range 表与端口表，生成只读视图。

修改与删除规则：

- Value 的宽度/符号修改建议新建 Value + `replaceAllUses`，避免破坏语义。
  - 若需要保留原 symbol，可先给新 Value 分配临时 symbol，完成 `replaceAllUses` 与端口更新后，`clearValueSymbol(old)` 再 `setValueSymbol(new, oldSym)` 完成迁移，避免重复。
- 删除无需拓扑顺序：循环可先用 `replaceAllUses` 或 `eraseOp(..., replacementResults)` 断开，再删除。
- 删除采取“标记 + freeze 压实”的策略；`freeze()` 负责清理死实体并重建连续布局。
- 如需保留旧 id，可在 `freeze()` 返回旧新 id 的映射；热路径只使用新 id。
- `useList` 的更新发生在 `GraphBuilder`（构建期）中；`GraphView` 为只读快照，不支持就地维护，`freeze()` 会重建热路径用的连续 useList。

典型使用方式：

```
GraphSymbolTable symtab;
GraphBuilder builder(symtab);
auto a = builder.addValue(symtab.symbol("a"), 8, false);
auto b = builder.addValue(symtab.symbol("b"), 8, false);
auto op = builder.addOp(OperationKind::kAdd, symtab.symbol("add0"));
builder.addOperand(op, a);
builder.addOperand(op, b);
auto out = builder.addValue(symtab.symbol("sum"), 8, false);
builder.addResult(op, out);
auto view = builder.freeze();
for (auto opId : view.operations()) { /* hot path */ }
```

## 端口与顺序语义

- 端口保持与现有 GRH 语义一致：输入/输出各自按名字排序用于导出。
- Graph 内部仍保存插入顺序（或显式 order），用于稳定遍历与序列化。
- 对优化阶段可维护额外的“拓扑顺序缓存”，在 freeze 时生成并缓存。

## 属性与调试信息

- 热路径尽量避免频繁解码属性，建议将属性存放在独立表中：
  - `AttrTable`：键值采用 `SymbolId` + 轻量 Value 形式，必要时延迟访问。
  - 可选提供按 key 的索引或缓存，用于 `opAttr(op, key)`；热路径默认仍用 `Span<AttrKV>` 顺序遍历。
  - `DebugTable`：`SrcLoc` 使用 side table，按需保留以降低常驻成本。

## 接口安全与一致性策略

- 所有变更通过 `GraphBuilder` 进行：插入/替换时同步更新 def-use 与端口绑定。
- 句柄带 generation 或 GraphId 校验，避免跨 Graph 误用。
- 只读路径不暴露可变引用，避免 bypass 一致性维护。

## 性能预期与收益点

- 遍历 Operation/Value 不再依赖哈希查找，更多命中连续内存。
- def-use 访问路径固定：Value -> userRange -> useList，无需额外解引用。
- `SymbolId` 与 `ValueId` 使用整数索引，适合批量处理与向量化遍历。

## 迁移路径建议

1. 引入 `SymbolTable` 与 `SymbolId`，将序列化与展示仍保留 string。
2. 引入 `ValueId/OperationId`，在现有结构外层提供句柄式访问。
3. 新增连续布局存储与 `GraphBuilder`，实现构建/冻结双阶段。
4. 逐步将 hot path 访问切换到 `GraphView`；保留旧结构用于调试/过渡。

## 风险与缓解

- 复杂性上升：通过分阶段迁移与单一入口（GraphBuilder）控制复杂度。
- 构建期与冻结期双结构维护成本：限制构建期可变操作，减少中间状态。
- 序列化一致性风险：保持 JSON schema 不变，新增一致性测试。

## 术语表

- Graph：GRH 中单个模块的图结构，包含 Value/Operation 与端口。
- Netlist：Graph 的集合，含顶层与跨图引用信息。
- Value：SSA 边，代表信号/连线的值。
- Operation：SSA 顶点，代表逻辑/存储/实例等操作。
- Symbol：人类可读的名字，驻留在 SymbolTable 中。
- SymbolId：Symbol 的整数句柄，Graph 内或 Netlist 内有效。
- GraphView：只读快照，用于热路径遍历。
- GraphBuilder：构建与变换入口，负责一致性维护与 `freeze()`。
- freeze：将构建期结构压平为连续布局并生成 GraphView。
- useList：Value 的用户列表，记录 `{OperationId, operandIndex}`。
