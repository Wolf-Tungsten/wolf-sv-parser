# GRH 表示 开发 OKR

## Objective 1: 改进Graph、Value、Operation 之间的索引方式，使用 symbol 进行索引

- KR1 修改 Graph 的文档、实现细节，将 Graph 的 moduleName 改为 symbol，语义不变
- KR2 为之后优化性能作准备，Netlist 中，直接用 Graph.symbol 索引 Graph 的 unique_ptr，去除原有的graphs_、graphByName_，graphAliasByName_ 改成 symbol 到 symbol 的索引，任何裸指针都不被允许
- KR3 改进 Graph 对于 Operation 和 Value 的持有方式，为 symbol 索引 Value 和 symbol 索引 Operation，禁止再使用裸指针
- KR4 Value 和 Operation 之间的关联关系全部用 symbol 索引，禁止使用裸指针
- KR5 修改后运行编译测试，确保所有测试仍然通过

## Objective 2: 在 GRH 中添加 Latch 支持

- KR1 docs/grh/dlatch_support_request.md 中描述了期望添加的 latch 类型
- KR2 文档层面添加原语的定义
- KR3 实现方面，扩展 grh.hpp/cpp
- KR4 实现方面，elaborate 对的组合逻辑分支，从报错改为生成 latch，但同时给出 warning
- KR5 实现方面，emit 要能支持 latch 的输出
- KR6 测试方面，补充测试样例