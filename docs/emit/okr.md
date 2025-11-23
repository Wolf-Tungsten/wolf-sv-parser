# GRH 表示输出 OKR

> 每次开始工作前，回顾 docs/overview.md 和 docs/GRH-representation.md
> 每次开始工作前，回顾前序阶段的完成情况（阅读docs/emit/phase*-report.md），在前序工作基础上开展

## 阶段1: 搭建 Emit 类框架
**Objective** 创建 Emit 基类，该类型负责所有 GRH 表示的输出，之后JSON和Verilog不同的输出路径均为 Emit类的子类
- KR1: 创建容纳 Emit 类的 src/emit.cpp 和 include/emit.hpp
- KR2: 为 Emit 基类搭建基本框架
- KR3: 创建 tests/data/emit 和 tests/emit 路径为之后的测试作准备