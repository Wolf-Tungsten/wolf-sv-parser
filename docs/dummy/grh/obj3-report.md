# Objective 3 完成报告：分层 SymbolTable 与基础句柄

## 完成事项
- 新增 `wolvrix::lib::grh::SymbolId`/`SymbolTable`，以及 `NetlistSymbolTable`/`GraphSymbolTable`，提供 `intern/lookup/contains/text/valid` API，invalid 约定为 0。
- 新增 `GraphId`/`ValueId`/`OperationId` 句柄，包含 generation + graph，并提供 `assertGraph` 用于跨图误用检查。
- SymbolTable 使用字符串驻留与稳定索引，`text()` 返回视图在表生命周期内有效；重复 `intern` 返回已有 id。
- 测试：`tests/grh/test_grh.cpp` 补充符号表查询/幂等 `intern` 行为以及句柄跨图断言用例。

## 构建与状态
- 已完成 `cmake --build build -j16`，全部目标构建通过。
- 未运行 `ctest` 回归。

## 后续建议
- 在后续 GraphBuilder/GraphView 接入时，明确 `GraphId` 的来源与 generation 递增策略。
- 逐步将跨图引用统一切换到 NetlistSymbolTable 管理的 `SymbolId`。***
