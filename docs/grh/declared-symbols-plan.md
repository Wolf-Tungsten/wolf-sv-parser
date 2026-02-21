# GRH declaredSymbols 方案记录

## 背景与动机

目前 GRH 的符号空间里，用户声明的信号与编译阶段生成的临时值
（如 `_expr_tmp_`、`__expr_*`、`__constfold__*`）混在一起。
这会带来以下问题：

- 优化后很难保证用户显式声明的 wire/var 不被删除。
- 很难区分“用户意图”与“编译器临时值”，不得不依赖名字前缀。
- emit/DCE 需要知道哪些符号是“必须保留”的。

## 目标

- 显式跟踪“用户声明的符号”。
- 不依赖临时命名约定。
- 为 DCE/emit 提供清晰的“保留依据”。
- 允许更新 JSON 结构以强制 declaredSymbols 与符号约束落地。
- **同步约束更新**：所有 graph 的 op/value 必须有有效 symbol；JSON load/emit 缺失
   symbol 视为硬错误。

## 方案概述

在 `wolvrix::lib::grh::Graph` 与 `wolvrix::lib::grh::Netlist` 两个层级增加 declaredSymbols 表，
但语义不同：

- Graph 级：记录“模块内部的用户声明符号”（wire/var/端口/存储等）。
- Netlist 级：记录“用户定义的模块名”（Graph 对应的 module 符号）。

推荐实现形态：

- `DeclaredSymbol` 集合（以 `SymbolId` 为 key）。
- Graph 级：与 `GraphSymbolTable` 并行存放，表示模块内声明集合。
- Netlist 级：与 `NetlistSymbolTable` 并行存放，表示“用户定义模块集合”。
- API 建议：
  - `void addDeclaredSymbol(SymbolId sym)`
  - `bool isDeclaredSymbol(SymbolId sym) const`
  - （可选）`std::span<const SymbolId> declaredSymbols() const`

Netlist 级 API 可对齐 Graph 级接口，但语义是“用户定义模块集合”，
Graph 级则表示“该 graph 内部声明集合”。

注意：Graph 级 declaredSymbols 表示“用户声明的信号名”，不代表一定是 Value，
也不等价于临时值或编译器生成的内部符号。
Netlist 级 declaredSymbols 表示“用户定义的模块名”，与 Graph 级语义不同，
不要求一一对应。

## Convert 阶段的填充规则

在 convert 阶段标记用户声明：

- Graph 级：
  - `NetSymbol` / `VariableSymbol`：加入 declaredSymbols。
  - 端口符号：加入 declaredSymbols。
  - 存储类声明（`kRegister/kLatch/kMemory` 的 symbol）也加入 declaredSymbols
    （它们是 Operation，但也应视为用户声明）。
- Netlist 级：
  - 对“用户定义的模块”（Graph）加入其 module symbol。

具体落点可在 `ModulePlan` 收集阶段或 Graph 组装阶段完成。

## Transform / Emit 接入策略

### Dead Code Elimination

对于 symbol 在 declaredSymbols 中的 Value，DCE 不应删除，即便：

- 没有 users
- 没有 defining op
- 不是端口

这样可以保证用户显式声明的 wire/var 不被优化掉。

### Emit

Emit 可以把 declaredSymbols 视为“必须具名保留”的候选。
当对应 Value 需要出现在输出中时，保证不会被内联/省略。
（具体策略可与 DCE 配合落地）

### 可配置行为

transform 是否保留用户声明的 symbol 应提供参数控制：

- 默认 **保留**（符合当前期望）。
- 允许显式关闭，以便进行更激进的优化或实验。

## 序列化策略（必须写入 JSON）

declaredSymbols 必须写入并从 JSON 恢复：

- graph 级新增 `declaredSymbols` 数组（模块内部声明）。
- netlist 级新增 `declaredSymbols` 数组（用户定义模块名）。

## 关联修改清单（必须同步落地）

以下修改与 declaredSymbols 方案配套，需要同步更新文档与实现：

- **符号强制性**：所有 graph 的 op/value 必须有有效 symbol，不再允许为空。
- **JSON 硬错误**：load/emit 遇到缺失 `symbol/sym` 直接报错。
- **文档同步**：更新 `docs/GRH-representation.md`、`docs/GRH-JSON-spec.md`、
  `docs/grh/ir-hot-path-design.md`、`docs/grh/okr-codex.md` 等涉及 symbol 约束的说明。

## 待讨论问题（结论）

- generate 里的显式声明：**视为用户声明**，加入 declaredSymbols。
- 隐式 wire（例如端口连接产生的隐式 net）：**不加入** declaredSymbols。
- inout 的 `__in/__out/__oe`：**视为编译器生成**，不作为用户声明。

## 内部符号命名规范（更新）

针对 **op/value 的内部生成符号**（非用户声明）制定统一规范：

1) **必须以下划线开头**（`_`），避免与用户符号冲突。
2) **必须区分 op 与 value**（固定前缀：`_op_` / `_val_`）。
3) **符号必须符合 Verilog/Verilator 信号命名规范**（合法标识符）。
4) **不再在 symbol 中携带 pass/purpose**；调试信息通过 `SrcLoc` 扩展字段记录。

推荐格式：

- op: `_op_<counter>`
- val: `_val_<counter>`

示例：

- `_op_17`
- `_val_42`

补充要求：

- `<counter>` 仅用于去重，保证 **graph 内唯一** 即可；不要求连续或全局唯一。
- 生成的完整 symbol 必须满足 Verilog 标识符规则（建议仅使用 `[A-Za-z0-9_]`）。

## SrcLoc 扩展（新增约束）

为便于调试，transform 生成的 op/value 必须填充 `SrcLoc` 的扩展字段：

- `origin`: 固定为 `"transform"`。
- `pass`: pass id（例如 `const-fold`、`redundant-elim`、`xmr-resolve`）。
- `note`: 简短用途说明（例如 `inline_const`、`pad_in`、`storage_read`）。

JSON 的 `loc` 字段扩展为 `{ file, line, col, endLine, endCol, origin, pass, note }`；
仅当字段非空时写出。

## 分阶段计划

### 阶段 1：约束与接口对齐

- 明确并固化“op/value 必须有 symbol、缺失即硬错误”的规范。
- 确认内部 symbol 命名规范（`_op_`/`_val_` + counter、合法标识符）。
- 明确 transform 生成节点的 `SrcLoc` 扩展字段规范（`origin/pass/note`）。
- 给出 Netlist/Graph declaredSymbols 的 API 设计与落点。

### 阶段 2：数据结构与 JSON 变更

- 在 Netlist/Graph 增加 declaredSymbols 存储与查询 API。
- Emit 写出、Load 读回 graph/netlist 级 declaredSymbols。
- JSON 解析遇到缺失 `symbol/sym` 直接报错。

### 阶段 3：Convert 填充与 Transform/Emit 行为

- Convert 按规则填充 Graph 级与 Netlist 级 declaredSymbols。
- Transform（DCE）默认保留 declaredSymbols，对应开关可关闭。
- Emit 对 declaredSymbols 的值优先保留具名表达，不被无意省略。

### 阶段 4：内部命名规范接入

- 统一内部 symbol 生成路径，确保 `_op_`/`_val_` + counter 规则落地。
- 所有内部生成符号满足 Verilog/Verilator 标识符约束并在 graph 内唯一。
- transform 生成节点统一写入 `SrcLoc.origin/pass/note`。

### 阶段 5：测试覆盖

状态：已完成。

覆盖点：

- JSON roundtrip 覆盖 declaredSymbols。
- 缺失 sym 报错的负例测试。
- declaredSymbols 保留行为与开关测试。
- 内部命名规范的格式/唯一性测试（`_op_`/`_val_` + counter）。
- transform 生成节点的 `SrcLoc` 扩展字段写入测试（如需要）。

## 实施进展（截至 2026-02-21）

已完成：

- 约束落地：op/value 必须有有效 symbol；JSON load/emit 缺失 sym 直接硬错误。
- 数据结构与 JSON：Graph/Netlist 新增 declaredSymbols 存取 API；JSON 写出与读取覆盖 graph/netlist 级 declaredSymbols。
- Emit 行为：JSON emit 强制使用 sym；SV emit 在进入时统一校验图内符号完整性，缺失即错误。
- Transform/DCE：默认保留 declaredSymbols；提供开关允许关闭保留。
- 内部命名规范接入：convert/const_fold/redundant_elim/xmr_resolve 等内部符号统一使用 `_op_`/`_val_` + counter；transform 生成节点同步写入 `SrcLoc.origin/pass/note`。
- 测试更新：修复无名 op/value 的测试用例；新增 declaredSymbols roundtrip、DCE 保留 declaredSymbols、内部命名规范格式/唯一性测试。

## 阶段对照检查（截至 2026-02-21）

### 阶段 1：约束与接口对齐

状态：已完成。

覆盖点：

- op/value 必须有 symbol，load/emit 缺失即硬错误。
- internal 命名规范已明确并接入实现（`_op_`/`_val_` + counter）。
- Graph/Netlist declaredSymbols API 已落地。

### 阶段 2：数据结构与 JSON 变更

状态：已完成。

覆盖点：

- Graph/Netlist declaredSymbols 存取 API。
- JSON 读写 declaredSymbols；缺失字段直接报错。

### 阶段 3：Convert 填充与 Transform/Emit 行为

状态：已完成。

覆盖点：

- Convert 填充 graph/netlist declaredSymbols。
- DCE 默认保留 declaredSymbols，提供开关关闭。
- Emit 对 declaredSymbols 强制保留具名声明。

### 阶段 4：内部命名规范接入

状态：已完成。

覆盖点：

- convert/const_fold/redundant_elim/xmr_resolve 等内部符号统一 `_op_`/`_val_` 规则。
- inout 辅助 value 符号由内部生成规则统一为 `_val_<counter>`。
- transform 生成节点统一写入 `SrcLoc.origin/pass/note`。

### 阶段 5：测试覆盖

状态：已完成。

覆盖点：

- declaredSymbols roundtrip 覆盖。
- DCE 保留 declaredSymbols 行为覆盖。
- 相关用例中无名 op/value 已修正。
- 内部命名规范的格式/唯一性专项测试。
- transform 生成节点 `SrcLoc` 扩展字段覆盖（如需要）。
