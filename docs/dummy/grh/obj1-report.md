# Objective 1 完成报告：GRH 索引全面 symbol 化

## 交付摘要
- Graph/Value/Operation/Netlist 全面改为以 symbol 为唯一索引，移除裸指针缓存与旧的 moduleName/name 字段。
- 容器改用 `unordered_map<symbol, unique_ptr>` + `valueOrder/operationOrder/graphOrder` 记录顺序；端口映射存储 ValueId，别名映射存储 symbol→symbol。
- Value/Operation 关联关系用 symbol 记录定义者与使用者；Operation operands/results 暴露句柄范围访问器。
- JSON 序列化/反序列化仅支持 symbol 字段（Graph.symbol、Value.sym、def、ports.val、ops.in/out），删除兼容字段。
- emit/elaborate/测试全量迁移到新 API，完成构建并通过全部编译单元。

## 主要改动点
- `include/grh.hpp` / `src/grh.cpp`
  - 引入 Symbol/ValueId/OperationId，Graph/Netlist/Value/Operation 改为 map 存储；增加 order 向量记录插入顺序。
  - Value 记录 `definingOpSymbol + users (opSymbol, idx)`；提供解引用访问器。
  - Operation operands/results 存 symbol，提供迭代视图与索引解引用。
  - Graph 只保留 `symbol()`；端口为 `map<string, ValueId>`；新增 getValue/getOperation 辅助。
  - Netlist 以 symbol 管理 Graph，alias 为 symbol→symbol，topGraphs 为 symbol 列表。
  - JSON 写入使用 symbol 字段；解析器仅接受新字段并更新端口/关联关系。
- `src/emit.cpp`
  - 按 graphOrder 遍历；Graph.symbol 输出；端口/ops 基于 order + symbol 序列化。
  - SystemVerilog emitter 使用 symbol 索引的遍历与端口宽度解析。
- Elaborate 栈与测试
  - 将 moduleName 引用替换为 Graph.symbol。
  - 所有端口/操作遍历改为通过 order + getOperation/getValue 访问；端口查找通过 symbol 解引用。
  - RHSConverter 等路径对 stateOp/results 使用 symbol 版接口。
- 测试更新
  - emit/elaborate/grh 单测改为 symbol 索引检查，用户/def 断言使用 op symbol。
  - 冒烟/层级/blackbox/stage21 等测试适配新容器与端口访问。

## 构建与状态
- 已完成 `cmake -S . -B build && cmake --build build`，所有目标（含测试可执行文件）构建通过。
- 由于项目未配置 ctest 用例，未运行额外测试命令；所有编译单元均成功。

## 后续建议
- 如需顺序敏感的输出/调试，继续沿用 `valueOrder/operationOrder/graphOrder`；严格避免依赖 map 遍历顺序。
- 若需要查漏旧字段/指针用法，可用 `rg "name\\("`/`rg "operations\\("` 等继续扫尾。***
