# GRH 表示 开发 OKR

## Objective 1: 改进Graph、Value、Operation 之间的索引方式，使用 symbol 进行索引

- KR1 修改 Graph 的文档、实现细节，将 Graph 的 moduleName 改为 symbol，语义不变
- KR2 为之后优化性能作准备，Netlist 中，直接用 Graph.symbol 索引 Graph 的 unique_ptr，去除原有的graphs_、graphByName_，graphAliasByName_ 改成 symbol 到 symbol 的索引，任何裸指针都不被允许
- KR3 改进 Graph 对于 Operation 和 Value 的持有方式，为 symbol 索引 Value 和 symbol 索引 Operation，禁止再使用裸指针
- KR4 Value 和 Operation 之间的关联关系全部用 symbol 索引，禁止使用裸指针
- KR5 修改后运行编译测试，确保所有测试仍然通过