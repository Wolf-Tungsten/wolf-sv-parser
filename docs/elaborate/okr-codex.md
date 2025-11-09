# Slang AST 到 GRH 转换 OKR

> 备注：JSON 序列化仅用于人工检查，自动化测试通过直接访问 GRH 内部结构完成。
> 每次开始工作前，回顾 docs/overview.md 和 docs/GRH-representation.md
> 每次开始工作前，回顾前序阶段的完成情况（阅读docs/elaborate/phase*-report.md），在前序工作基础上开展
> 需要与 slang 交互时，阅读 external/slang 中源码
> 可参考 docs/reference/slang-netlist docs/reference/verilator docs/reference/yosys 实现中的算法

## 阶段8：创建 RHSConverter 基类
- **目标** 把 slang 的右值表达式翻译成 GRH Operation/Value 链，并即时挂到当前模块的 GRH 图中。
- **接口** 基类需要同时拿到 AST 表达式节点、正在构建的 GRH 图以及 memo 上下文，方便在转换过程中随时读取缓存信息。
- **可扩展性** 该类只是基座，后续会派生 CombRHSConverter 和 SeqRHSConverter，分别服务组合逻辑与时序逻辑场景。
- **表达式覆盖** slang 支持的所有 RHS 形态都要落到 GRH Operation/Value 链，并最终返回一个表示结果的 GRH Value。
- **net/reg 访问** 若表达式中读取 net 或 reg，必须通过 memo 取出相应的 GRH Value，而不是重新解析。
- **验证** 需要准备能覆盖上述行为的测试样例，确认接口、派生关系、表达式覆盖以及 memo 读值都按照预期工作。
