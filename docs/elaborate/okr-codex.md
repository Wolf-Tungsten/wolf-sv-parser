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

## 阶段10：设计实现 Value 写回 memo 数据结构
- **动机** 连续赋值与过程块的 LHS 可以是切片、层级结构体或拼接表达式，若直接覆写 net/reg memo 中已 flatten 的 Value，会破坏 GRH 的 SSA 约束。因此阶段10要引入 writeBack memo，先收集「写意图」，待所有 assign/always 解析完毕后再统一生成 kConcat 或直接赋值到既有 Value。
- **KR1（理解并文档化）** 需要彻底梳理现有 net/reg memo：前者记录 flatten 后的 Value handle（位宽、bit span、来源符号），后者还有寄存器/内存 Operation 句柄与驱动分类。基于这些事实设计 writeBack memo 的结构，并在 `docs/elaborate/memo.md` 中把三类 memo 的字段、生命周期、互相引用关系一次性说明，方便后续维护。
- **KR2（数据结构设计）** writeBack memo 至少要描述：目标符号（指向 net/reg memo 条目）、写入 bit 区间/字段路径、来源 Value（通常是 RHSConverter 结果）以及 assign 类型（连续/过程块）和优先级。设计要兼顾两条路径的效率：解析阶段能快速 append 新条目，不做昂贵 flatten；finalize 阶段能根据 bit 覆盖情况批量生成 concat/覆盖操作，避免逐位拼接。
- **KR3（代码实现）** 在 elaborate 管线里落地该数据结构及其 API：提供记录写意图的接口、解析结束后的 finalize 逻辑（遍历 writeBack memo，生成对应的 GRH Operation/Value 更新，必要时回写 net/reg memo 中的引用），并确保与现有 memo 的引用保持一致性（如写回寄存器时链接到 kRegister* stub 的 data operand）。同时补充基础测试，验证 writeBack memo 可同时处理多种 LHS 形态且不会破坏 SSA。

## 阶段11：支持解析纯组合逻辑的 assign 连续赋值
- **目标定位** 在阶段8-10的基础上，让 `assign`（无时序语义）语句完整走通：RHS 借助 CombRHSConverter 生成表达式 Value，LHS 则透过 writeBack memo 记录最终要落地到哪些 net memo 位段，实现真正的「纯组合」驱动路径。
- **KR1（RHS 解析接入）** 解析 `assign lhs = rhs;` 时直接复用 CombRHSConverter：把 RHS 的 slang AST、当前模块 Graph、net/reg memo 上下文交给转换器，拿到一个经过 flatten/拼接后的 `Value`。需要确保 RHS 中涉及的 net/reg 或 memory 读都遵循阶段9的规则，且 assign 遇到还未支持的 RHS 类型时要给出清晰诊断。
- **KR2（LHS 切片与 writeBack）** LHS 可能是标量、结构体字段、数组元素甚至 concat，需要基于 TypeHelper 的位段信息构建必要的 kSlice*/kConcat 操作来对齐 RHS 位宽，再把「写意图」写入阶段10的 writeBack memo。流程包含：解析 LHS -> 映射到 net memo 中的目标 Value 与 bit 区间 -> 记录赋值类型为“组合连续”，为 finalize 阶段生成实际的 Value 覆写/拼接提供完整上下文。
- **KR3（回归与可视化）** 新增覆盖标量/结构体/数组/concat 等 LHS 场景的回归样例，重点验证：1）RHS 转换结果位宽与目标匹配；2）writeBack memo 最终能生成正确的 kConcat/kSlice 链路；3）连续赋值对已有 SSA 结构无副作用。测试过程中沿用前几阶段的做法，输出可读 JSON（或等效文本）来展示 assign 解析后写入 writeBack memo 以及最终 Graph 的状态，方便人工审查。
