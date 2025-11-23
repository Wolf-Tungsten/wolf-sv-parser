# GRH 表示输出 OKR

> 每次开始工作前，回顾 docs/overview.md 和 docs/GRH-representation.md
> 每次开始工作前，回顾前序阶段的完成情况（阅读docs/emit/phase*-report.md），在前序工作基础上开展

## 阶段1: 搭建 Emit 类框架
**Objective** 创建 Emit 基类，该类型负责所有 GRH 表示的输出，之后JSON和Verilog不同的输出路径均为 Emit类的子类
- KR1: 创建容纳 Emit 类的 src/emit.cpp 和 include/emit.hpp
- KR2: 为 Emit 基类搭建基本框架
- KR3: 创建 tests/data/emit 和 tests/emit 路径为之后的测试作准备

## 阶段2: 将 JSON 生成迁移到 Emit 类中
**Objective** 将之前在 Elaborate 和 GRH 中实现的 JSON 输出剥离，统一到 EmitJSON 中
- KR1: 规划适合GRH表示的 JSON 数据格式，注意要保持可读性的前提下，尽可能缩减文件体积和行数，缩短重复的 key 的名称，像 Value 这种小的数据结构应当一行输出，形成文档写入 docs/GRH-JSON-spec.md 供参考
- KR2: 迁移 JSON 输出逻辑，统一到 EmitJSON 中
- KR3: 更新现有的 test，均添加由 EmitJSON 支持的 JSON 输出