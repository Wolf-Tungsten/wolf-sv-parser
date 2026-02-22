# GRH Symbol/Port 机制重构执行计划

## 状态与决策
- intern 语义采用**方案 A（严格）**：`internSymbol` 若发现符号已绑定到 value/op，返回 invalid。
- **保留 bindSymbol 冲突检测**：create* 内仍会在 bind 冲突时抛异常（安全兜底）。
- createOperation/createValue 仍会因无效符号、跨图符号、宽度非法等问题抛异常；避免异常的推荐路径是：
  `sym = internSymbol(name)` 且 `sym.valid()` 后再 create。

## 目标
- 端口名不再参与 Graph 的 SymbolId 体系，减少不必要的符号占用与冲突。
- 将 value/op 符号冲突检测前置到 intern 阶段，避免常规路径在 create* 中抛异常。
- createOperation/createValue 保持抛异常语义，但只作为安全兜底与非法输入处理。

## 范围与非目标
- **范围**：端口 API/数据结构、intern 语义、相关 JSON load/store/emit、测试与文档同步。
- **非目标**：移除 bindSymbol 机制；将 create* 改为返回 invalid 代替异常。

## API 变更清单
- `Port`/`InoutPort`：`name` 从 `SymbolId` 改为 `std::string`。
- 端口绑定 API：
  - `bindInputPort/bindOutputPort/bindInoutPort` 接口改为 `std::string_view name`。
  - `inputPortValue/outputPortValue` 改为接受 `std::string_view`。
  - 新增端口移除 API：
    - `removeInputPort(name)` / `removeOutputPort(name)` / `removeInoutPort(name)`。
    - 可选 `clearPorts()` 或分别清空三类端口。
- `internSymbol`：严格语义（已绑定则返回 invalid）。

## 行为变更摘要
- **internSymbol**：
  - 已 intern 且未绑定 value/op -> 返回已有 ID。
  - 已 intern 且已绑定 value/op -> 返回 invalid。
  - 未 intern -> 分配新 ID。
- **createOperation/createValue**：
  - 仍会在无效 SymbolId、跨图等非法输入时抛异常。
  - 仍会在 bind 冲突时抛异常（兜底）。
  - 正常路径通过 intern 拦截冲突，避免异常。
- **端口名**：不进入 SymbolId 体系，不占用 value/op 的符号命名空间。

## 实施步骤

### Step 1: 端口数据结构与 API 改造
- `Port/InoutPort` 的 `name` 改为 `std::string`。
- 修改端口绑定/查询 API 的参数类型。
- 新增端口移除 API，并确保移除时同步清理 value 的 in/out/inout 标记。
- 更新 GraphBuilder/GraphView/Graph 的缓存结构与遍历逻辑。

### Step 2: JSON load/store/emit 同步
- 端口名改为字符串读写。
- 调整 `Graph::writeJson`、`LoadJson` 的端口解析逻辑。
- 更新相关测试与断言。

### Step 3: Symbol 语义调整
- 修改 `Graph::internSymbol` 实现，按严格语义检测 bind 冲突并返回 invalid。
- 更新调用点：
  - 需要已有 SymbolId 的场景改用 `lookupSymbol`。
  - 新建 value/op 场景在 `internSymbol` 之后检查 `valid()`。
- 更新文档说明（grh-api + 相关 dummy docs）。

### Step 4: createOperation/createValue 行为对齐
- 保留异常路径（无效 SymbolId、跨图、宽度非法）。
- 保留 bind 冲突异常作为兜底，但确保常规路径通过 intern 避免异常。

### Step 5: 测试与回归
- 新增/更新测试覆盖：
  - 端口名不进入 symbol table（同名端口不占用符号）。
  - `internSymbol` 冲突时返回 invalid。
  - 使用无效 SymbolId 调用 create* 抛异常。
  - bind 冲突时 create* 抛异常（兜底）。
- 全量 `ctest` 回归。

## 风险与回滚点
- 行为变化是 API 破坏性改动：需更新所有调用与文档。
- 端口名改为 string 会影响 JSON/emit/ingest 路径，需回归确认。

## 交付物
- 代码：
  - `lib/include/grh.hpp` / `lib/src/grh.cpp`
  - JSON load/store/emit 路径
- 文档：
  - `docs/grh/grh-api.md`
  - 相关 dummy 记录
- 测试：更新现有 grh/emit/store 测试用例
