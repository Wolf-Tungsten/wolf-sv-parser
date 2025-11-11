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

## 阶段12：创建 CombAlwaysConverter 类
- **目标定位** CombAlwaysConverter 补全 `always_comb` / `always @(*)` 过程块的解析链路，是 `processCombAlways` 的核心执行器，负责把整个过程块转成 GRH Operation/Value 并与 writeBack memo 协同。
- **KR1（入口与生命周期）** CombAlwaysConverter 需要接受当前过程块的 AST、模块 Graph、net/reg/writeBack memo 以及诊断接口，在被 `processCombAlways` 调用时完成初始化、遍历语句、收敛结果的全流程；要求与 assign 流程共用 finalize 机制，且能为每个过程块生成独立的写集合，便于调试。
- **KR2（阻塞赋值语义）** 过程块内部要维护「就地生效」的阻塞赋值语义：同一块里的后续语句读取到的是最新写入值。实现上需提供块级 memo shadow（临时 Value map），记录 `symbol -> Value` 的即时覆盖，并在语句解析时优先查询 shadow；块结束时再把最终值写入全局 writeBack memo。
- **KR3（专属 RHS/LHS 支持）** 基于阶段8-11的成果扩展出 CombAlwaysRHSConverter（复用 CombRHSConverter 能力但接入块级 shadow）以及匹配的 LHS 处理逻辑，保证 `a = b[c + 1]` 这类组合场景能正确读取 shadow、生成必要的 kSlice*/kConcat，并把结果登记到 writeBack memo，记录其过程块来源和执行顺序。
- **KR4（Graph 写入与诊断）** 解析完成后，要把过程中生成的 Operation/Value 插入当前 Graph，并将最终赋值通过 writeBack memo 统一落地；同时输出诊断/trace 信息（或 JSON dump）以便验证：生成节点、shadow 命中、写集合等细节都与 SystemVerilog 语义一致。
- **KR5（控制流扩展留钩子）** 预留 `TODO`/扩展点以支持未来的 if/case/loop：结构化地列出需要覆盖的 slang AST 节点、shadow 与 writeBack memo 如何配合、多分支合流时的策略等，让后续阶段可以在既定接口上继续实现，不需要重写 CombAlwaysConverter。
- **KR6（LHSConverter 抽象计划）** 参考 RHSConverter 体系，把当前 `ContinuousAssignConverter` 内的 LHS 解析/切片逻辑提炼成 `LHSConverter` 基类，负责公共的字段路径推导、kSlice/kConcat 生成。随后派生 `ContinuousAssignLHSConverter` 和 `CombAlwaysLHSConverter`，分别处理连续赋值与组合过程块的写回策略（前者直接写入 writeBack memo，后者写入 block shadow 并在块结束统一落地），以降低两条管线耦合并为未来 SeqAlways 等场景复用铺平道路。

## 阶段13：CombAlwaysConverter 添加 if/case 支持
- **总体目标** 在阶段12的基础上补齐组合过程块里的结构化控制流，实现可综合 `if`/`case` 在 GRH 上的正确映射，并仍旧复用 shadow + writeBack memo 的执行模型。
- **KR1（算法对齐文档）** 需要深入研读 `docs/reference/yosys` 中对条件/多路分支的处理方法，形成一份内部可引用的笔记（新文档 process-always-comb.md），明确：条件表达式如何规约成位宽一致的布尔 Value，case 项如何排序/优先级化，default 分支如何兜底，以及 yosys 如何在 SSA 视角避免 latch。
- **KR2（kMux 生成）** CombAlwaysConverter/CombAlwaysRHSConverter/CombAlwaysLHSConverter 要能根据 if/case 的控制流在 GRH 中创建 kMux/kMuxChain 结构：对 if/else 形成二路 mux，对 case 形成链式mux（不允许出现 1：n mux），并在必要时增强 CombAlwaysLHSConverter，使其能对每个分支单独准备写入 Value，最终在控制流合流点用 mux 把 Value 合成后再写入 shadow/writeBack。
- **KR3（防止 latch）** 仅支持完全覆盖的可综合结构：被赋值的变量要完整分支覆盖；缺失覆盖将导致潜在 latch，需要在解析阶段检测并报错。对于显式声明的优先级 case/unique case，要按照 SystemVerilog 语义（短路/优先 vs 并行）生成 mux，并确保诊断信息能够提醒用户不支持的 latch 模式。
- **KR4（测试与复杂样例）** 设计涵盖嵌套 if/case、结构体字段赋值、带 shadow 读写的例子，以及 case 内部调用 RHSConverter 的复杂表达式；测试需输出 Graph/写回 memo JSON，证明 mux 链路、shadow 更新、错误分支都按预期运行，并覆盖 latch 检测的负例。

## 阶段14：CombAlwaysConverter 添加静态分支分析能力
- **目标定位** 在阶段13的控制流框架上补齐可静态决策分支（条件/ case item 在 elaboration 期间即可求值）的优化路径：被判定为「恒真/恒假」的控制块在 GRH 中不再注入多余的 kMux，而是直接落到存活分支的 Value/写集合。
- **KR1（静态条件检测与折叠）** 结合 slang 的常量折叠能力，在处理 if/case 节点时先尝试对条件表达式求 `ConstantValue`，若能判定为 0/1 或匹配到唯一的 case item，则只解析对应分支并标记其它分支为 unreachable；同时阻止 CombAlwaysRHSConverter/CombAlwaysLHSConverter 生成冗余 Value，确保 Graph 中看不到因静态条件被动创建的 kMux/kMuxChain。
- **KR2（静态/动态混合嵌套）** 当静态分支内部仍包含动态控制（或反之），需要保证 shadow/writeBack memo 的可见性规则依旧成立：静态剪枝负责缩小语句集合，但对残存的动态分支继续沿用阶段13的 mux 合成逻辑，并能正确溯源「静态祖先」信息，用于诊断和调试。
- **KR3（测试覆盖）** 构建覆盖纯静态条件、静态 case、静态与动态交错（静态外层包裹动态子 if/case、动态 case item 中存在静态 guard）的单元测试，验证生成的 GRH/写回 JSON 中确实没有多余 mux，并补充断言证明 unreachable 分支不会错误触发写回。

## 阶段15：CombAlwaysConverter 添加循环展开能力
- **目标定位** 让 CombAlwaysConverter 能够解析 `always_comb`/`always @(*)` 内部的 for/foreach 循环，并在 elaboration 阶段把它们完全展开成静态语句序列，使 shadow memo 与 writeBack memo 仍遵循逐语句推进的模型。
- **KR1（算法规划文档）** 研读 `docs/reference/yosys` 中关于循环的处理方式，补写一份 `docs/elaborate/process-comb-loops.md`（或扩展既有 process 文档），明确：循环入口条件、迭代表达式、循环变量作用域、嵌套循环与 foreach（数组/结构体字段枚举）的展开顺序，以及与 shadow/writeBack 的交互约束；文档需列示允许/禁止的 SystemVerilog 语法，并给出错误诊断策略。
- **KR2（编译期展开实现）** 在 CombAlwaysConverter 中实现基于常量上下限的循环 unroll：for 循环需要在 elaboration 期间计算初值/终值/步长，生成按迭代顺序串联的语句副本；foreach 循环依赖 TypeHelper 提供的字段/索引列表，逐个展开并绑定对应的迭代变量。展开过程中要为每个迭代实例创建独立的 shadow 层级（或在同一 shadow 中带迭代编号），确保读取到的写入与真实执行顺序一致，且在无法确定迭代次数时及时报错。
- **KR3（静态 break/continue）** 对 `break`/`continue` 语句做静态判定：当触发条件可在 elaboration 阶段求值时，直接剪裁后续迭代或跳过当前迭代余下语句；无法静态确定的情形应报错，提示需要可决策的循环界限。需支持 for/foreach 中嵌套 break/continue，并记录诊断上下文（循环标签若存在也需校验）。
- **KR4（测试与可视化）** 构建覆盖 for、foreach、嵌套循环以及带 break/continue 的组合样例，验证展开后生成的 Operation/Value 顺序、shadow 命中与 writeBack 结果。测试应输出 JSON/trace，展示每个迭代实例的写集合，确保循环展开不会遗留动态结构，也不会破坏 SSA 或写回顺序。

## 阶段16：创建 SeqAlwaysConverter 类
- **定位** 针对含 posedge/negedge 敏感表或 `always_ff` 的时序过程块提供 SeqAlwaysConverter/SeqAlwaysLHSConverter，确保 sequential 语义与 net/reg memo、writeBack memo 的职责边界清晰，并与阶段15结束时的 elaborate 框架顺畅衔接。
- **流程接入（KR1）** 按 docs/reference/yosys 的判定规则梳理何时触发 `processSeqAlways`：检测敏感列表、判定时钟/复位信号、从 elaborate 主流程传入 graph+memo+诊断上下文，让 SeqAlwaysConverter 完成语句遍历；需要同步补全 `isSeqProceduralBlock` 之类的判断与错误提示。
- **公共基类（KR2）** 抽象 AlwaysConverter 基类，收敛 if/case/loop 分支栈、块级 shadow memo、RHS/LHS Converter 工厂等公共逻辑；CombAlwaysConverter 与 SeqAlwaysConverter 只覆写专属的 RHS/LHS Converter 创建、赋值调度策略以及 finalize 钩子，重构后跑回归确保组合流程行为不变。
- **非阻塞语义（KR3）** SeqAlwaysConverter/SeqAlwaysLHSConverter 仅支持非阻塞赋值：遇到阻塞赋值立即诊断；块内维护「当前时钟沿」的 writeBack 列表，记录每条非阻塞语句的目标符号、bit 范围、优先级，并在多次写同一目标时按源代码顺序合并。
- **finalize 规划（KR4）** 规划 finalize 的三步走但暂以 TODO 落地：1）遍历 writeBack memo，把寄存器条目的 RHS Value 连接到对应 kRegister* stub 的 data 输入（同步/异步复位钩子预留 TODO）；2）为 reg memo 中的 kMemory 条目生成 kMemorySyncRead/Write 端口，明确地址/数据/使能 Value 的来源并记录 TODO 占位以便后续实现真正连线；3）生成人工可读的阶段日志或 JSON dump，输出 register/memory 绑定结果与待实现项，方便阶段评审。
