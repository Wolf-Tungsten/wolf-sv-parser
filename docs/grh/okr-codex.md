# GRH 表示开发 OKR


# Objective 1

> 目标：彻底用 symbol 索引 Graph/Value/Operation，移除所有裸指针与旧命名，不保留向后兼容路径。

## 原则与顺序
- 统一 symbol 规范（命名规则、唯一性、序列化字段），在 `docs/GRH-representation.md` 与头文件注释落地。
- 依赖顺序：Graph 命名重命名 → Netlist 容器与 API 重写 → Graph 内部 Value/Operation 存储重写 → Value/Operation 关联关系纯 symbol 化 → 全量测试。
- 不留旧字段/别名/兼容 JSON；改动一次到位，保证实现与文档同步。

## KR1：Graph 的 moduleName 直接替换为 symbol
- 字段与接口：Graph 仅保留 `symbol_` 成员与 `symbol()` 访问器；构造函数参数改为 `symbol`，删除 name/moduleName 相关字段与别名。
- 使用点清理：更新 `include/grh.hpp`、`src/grh.cpp`、elaboration/emit 流程、测试用例和 `docs/GRH-representation.md`、`docs/grh/okr.md`；所有调用改用 `graph.symbol()`。
- 序列化：JSON 仅使用 `symbol` 字段，移除 moduleName fallback；旧格式不再支持。

## KR2：Netlist 以 symbol 管理 Graph（无裸指针）
- 容器形态：`graphs_` 仅存 `std::unordered_map<std::string, std::unique_ptr<Graph>>`，对外不暴露指针。
- API：`createGraph(symbol)` 校验唯一性并返回 `Graph&`；`findGraph(symbol)` 返回可空引用包装或迭代器；alias 映射调整为 `unordered_map<symbol, symbol>`；`topGraphs_` 存 symbol 列表。
- 生命周期：移除 `graphByName_`、`graphAliasByName_` 裸指针缓存；移动/拷贝语义在重写后确保 Graph.owner 重新绑定。

## KR3：Graph 内部的 Value/Operation 存储按 symbol 索引
- 句柄：定义 `ValueId/OperationId = std::string`（后续可替换为强类型）；Graph 内部用 `unordered_map<ValueId, size_t>`/`unordered_map<OperationId, size_t>` 映射到 `values_`/`operations_` 的 `unique_ptr` 下标。
- API：`createValue/createOperation` 只接受 symbol 与属性，内部注册句柄并返回引用；`find*` 返回可空引用包装；遍历接口提供 const view（不暴露裸指针），需要可变访问时通过句柄解引用。
- 端口绑定：`inputPorts_/outputPorts_` 存 ValueId；绑定/查询/序列化均走 symbol。

## KR4：Value 与 Operation 关联关系完全 symbol 化
- 数据成员：Value 的 `definingOp`、`users` 全部改为 `OperationId + operandIndex`；Operation 的 `operands/results` 存 `ValueId` 列表。
- 辅助访问：Graph 负责通过句柄解引用并维护一致性（添加/替换/移除时同步更新 users）；在接口层仅暴露 const 引用或句柄，不再暴露裸指针。
- 行为调整：`addOperand/addResult/replaceOperand/replaceResult` 接收引用/句柄后即转换为 symbol 存储；JSON 序列化/反序列化基于 symbol 写关系。

## KR5：验证与收敛
- 单测与断言：新增覆盖 symbol 查找、alias 解析、端口绑定、用户列表更新、移动语义下 owner 重绑的测试；引入断言防重复 symbol、悬空引用。
- 回归：完成重写后运行 `ctest`（或 `ninja test`）确保行为一致；必要时新增 JSON roundtrip 测试确认仅有 symbol 字段。
