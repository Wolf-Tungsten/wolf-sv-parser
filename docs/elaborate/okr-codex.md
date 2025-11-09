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

## 阶段9：创建 CombRHSConverter 派生类
- **定位** CombRHSConverter 继承自阶段8的基类，用于组合逻辑 RHS 的具体实现，核心是把各种读取、拼接、切片操作映射到 kSlice*/kConcat/kMemoryAsyncReadPort 等原语。
- **KR1（复杂 net/reg 读取）** 对结构体/数组/嵌套类型的读取需要借助 TypeHelper 提供的 flatten 元信息，按照 bit 范围建立 kSlice* 链路；要保证 memo 中记录的 Value 可以被逐层拆解，还要关注字节序和 packed/unpacked 的位偏移。
- **KR2（内存读取）** 当 RHS 涉及 mem（reg memo 中标记为 kMemory 的条目），需要生成 kMemoryAsyncReadPort Operation；这意味着 CombRHSConverter 要能识别地址/掩码等输入来源，并在 GRH 图中注入一个异步读端口节点，输出 Value 回填到表达式求值路径。
- **KR3（简单 concat/slice）** 对标量或扁平化后的 net/reg，需支持组合逻辑层面的按位切片和拼接，常见于连续位段选取、{a,b} 形式；实现上要确保位宽校验、GRH Value 顺序正确以及与 TypeHelper 的一致性。
- **KR4（测试与人工审查）** 测试集需覆盖上面三类场景，并把 CombRHSConverter 生成的 Operation/Value Graph 导出成可读 JSON 供人工对照；自动化测试继续走 ctest，重点校验 slice/concat/memory read 的位宽与连接是否符合预期。
