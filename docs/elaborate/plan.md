# Slang AST 到 GRH 转换实施计划

分为以下几个阶段

1. 参考文档，建立 GRH 表示的数据结构实现，并添加数据结构序列化成json的支持
2. 搭建 elaborate 框架，能够读取 slang ast，生成 GRH 表示数据结构，并提供诊断信息支持（当某个特性尚未实现时提示），支持ctest进行smoke测试
3. elaborate 框架支持 slang ast 模块层次结构的解析，据此建立层次化的 GRH 表示
4. elaborate 框架添加对 assign 赋值语句的支持，此过程包含对表达式的解析
5. elaborate 框架添加对 always_comb always@(*) 组合逻辑行为描述的支持（不需要支持 always_latch）
6. elaborate 框架添加对组合逻辑行为描述中 if/case/loop 的静态分析及展开
7. elaborate 框架添加对 always_ff always 等时序行为描述的支持，能够解析其中的 if/case/loop 结构
8. elaborate 框架能够对时序逻辑中寄存器进行识别，生成 kRegister*
9. elaborate 框架能够对时序逻辑中存储器进行识别，生成 kMemory 及读写口
10. elaborate 框架能够对时序逻辑中$display进行识别，生成 kDisplay
11. elaborate 框架能够对仿真终止条件$finish、$error 等进行识别，生成 kAssert
12. elaborate 框架能够对 dpi 进行识别，生成 kDpicImport 和 kDpicCall