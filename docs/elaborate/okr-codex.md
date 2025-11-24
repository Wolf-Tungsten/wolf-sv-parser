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
- **finalize 规划（KR4）** 规划 finalize 的三步走但暂以 TODO 落地：1）遍历 writeBack memo，把寄存器条目的 RHS Value 连接到对应 kRegister* stub 的 data 输入（同步/异步复位钩子预留 TODO）；2）为 mem memo 中的 kMemory 条目生成 kMemorySyncRead/Write 端口，明确地址/数据/使能 Value 的来源并记录 TODO 占位以便后续实现真正连线；3）生成人工可读的阶段日志或 JSON dump，输出 register/memory 绑定结果与待实现项，方便阶段评审。

## 阶段17：SeqAlwaysConverter 类处理寄存器非阻塞赋值
- **目标定位** 接棒阶段16的 finalize TODO，把 SeqAlwaysConverter 在遇到寄存器非阻塞赋值（NBA）时的结果真正落到 kRegister 节点：同一 always_ff/时序块内要基于块级上下文一次性绑定 clk/rst/d，并补齐 `reg memo -> kRegister` 之间的实际连线。
- **KR1（kRegister 操作数绑定）** 在解析 NBA 时即收集目标寄存器、写入 bit 区间与 RHS Value，finalize 阶段按寄存器聚合：对每个 reg memo 条目填充 kRegister 的 data/clk/rst operand。clk/rst 来源于 `processSeqAlways` 已解析的敏感列表信息，data 则来自本块 writeBack 列表的拼接结果，确保单一寄存器的所有操作数都在本阶段完成设置。
- **KR2（属性与状态校验）** finalize 前对 reg memo 条目进行自检：确认类型为 kRegister（非 memory）、attribute 与当前块的时钟/复位匹配（极性、同步/异步）、各 operand 尚未被其他块写入。任一条件不满足需报错并指明冲突来源，以防止重复绑定或把组合逻辑误连到寄存器口。
- **KR3（分段不重叠合成）** 同一 always 块里对同一寄存器的多段非重叠写入需要按照 bit 位顺序构造 kConcat，把每段 RHS Value 对应的 slice 拼出完整 data 输入。实现上依赖 TypeHelper/flatten 信息生成 `[hi:lo]` 描述，并在 finalize 时根据覆盖区间排序合并，确保拼接后的位宽与寄存器定义一致。
- **KR4（重叠写入覆盖规则）** 若同一寄存器在一个块内被重复赋值或区间重叠，后出现的赋值覆盖先前结果。需要在 writeBack 结构中携带语句顺序，合并时按顺序覆写重叠区间（无需报错），并在调试日志中标注覆盖关系，便于追踪最终 data 值的来源。
- **KR5（保留未写片段）** 当一个寄存器只被切片赋值且未覆盖全部位段时，未触碰的片段要保留上一拍的 q 值：通过从现有 kRegister 输出切片生成保留 Value，再与新的 data 片段拼接，保证整体 data 输入仍覆盖全位。如果 q 尚未可用（例如尚未插入 Graph），需要在 finalize 前确保可以获取该 Value 或给出明确的 TODO/诊断。
- **验证与可视化** 增加覆盖单寄存器多段写入、重叠写入、部分写入保留旧值以及不同复位形态的时序样例，测试中 dump 出寄存器绑定的 clk/rst/data Value 及每段写入的位宽，确保 kRegister 的三个 operand 都按预期被设置；必要时扩展现有 JSON trace，帮助评审核对覆盖/保留策略。

## 阶段18：SeqAlwaysConverter 类处理 memory 读写口
- **目标定位** 延续阶段16-17 的 SeqAlwaysConverter 能力，把时序 always/always_ff 中涉及 `mem memo` 上标记为 kMemory 的条目落到 GRH 的同步读/写端口，参考 `docs/reference/yosys` 的做法确保 memory 行为在单个块内即可完全建模。
- **KR1（同步读口建模）** 当 SeqAlwaysConverter/SeqAlwaysRHSConverter 解析到 memory 读取（含 `mem[addr]`、结构体/数组字段读取）的 RHS，用 TypeHelper 提供的 bit 布局生成地址/数据 Value，并创建 `kMemorySyncReadPort` Operation。端口需绑定 memory handle、块级时钟、地址和可选的 enable；若 AST 中未显式出现 enable，则自动接 `Constant(1)`，并在 trace 中记录推断来源。
- **KR2（整字写口落地）** 对于 `mem[addr] <= data;` 这类完整字写入，SeqAlwaysLHSConverter 需要把 RHS flatten 成与 memory 数据位宽一致的 Value，finalize 阶段生成 `kMemoryWritePort`，填入 memory、clk、addr、en、data 五个 operand，en 缺省同样连常量 1。需要支持同一 always 块写多条 memory 语句：按源顺序记录写意图，最终为每条语句创建独立端口并保留引用，方便后续优化或冲突检查。
- **KR3（按位写掩码合成）** 当出现 `mem[addr][i] <= data_bit;` 或对数据位段的部分写入时，先在块内把每次写入记录到一个「mask write」集合，记住 addr 表达式、bit/byte 范围与 RHS Value。finalize 时解析这些集合并按照 SystemVerilog 语义合成 `kMemoryMaskWritePort`：生成 mask Value（标记哪些 bit 被覆盖）与 data Value（被写入的新值），同样绑定 clk/en。需要对同一 addr/bit 的多次写入按语句顺序覆盖，并在日志中显示最终 mask/data。
- **KR4（测试与可视化）** 构建覆盖仅读取、整字写入、按位/按字节写入混用、显式 enable 与隐式 enable 的时序样例；回归中验证生成的 kMemorySyncReadPort/kMemoryWritePort/kMemoryMaskWritePort 数量与连接是否正确，dump JSON/trace 展示每个端口的 clk/addr/en/data/mask 取值来源，确保 memory 行为在 GRH 中可审计。

## 阶段19：SeqAlwaysConverter 类处理 if/case 语句
- **目标定位** 在现有 SeqAlwaysConverter/AlwaysConverter 能力之上，补齐分支语句语义，形成可落地到 GRH 的复位/使能与内存端口行为。
- **KR1（分支覆盖）** if/case 解析
  - 已有能力：`visitConditional`/`visitCase` 支持静态折叠与动态合流（生成 `kMux`、`kEq`、`kOr`、通配比较）；保留现状，补充回归样例覆盖 `if-else`、`case/unique case`、`casez/casex`。
  - 新增：为分支执行建立「条件保护」上下文 guard。维护 `currentGuard`（1-bit Value），进入 `if` 的真/假分支分别令 `guard = parentGuard & cond`、`guard = parentGuard & ~cond`；进入 `case` 分支令 `guard = parentGuard & match_i`，default 分支令 `guard = parentGuard & ~(match_0 | ... | match_n)`。
  - 支撑算子：在 `AlwaysConverter` 内新增 `buildLogicAnd(lhs,rhs)`、`buildLogicNot(x)`，与现有 `buildLogicOr`/`buildEquality` 风格一致，结果宽度固定为 1。
- **KR2（寄存器复位/使能）**
  - 复位：沿用阶段17的 `buildResetContext` + `extractResetBranches`，从 data 路径的 `kMux` 中提取 rst 条件与 `resetValue`，分别绑定到 `kRegisterRst/kRegisterArst` 的 `rst/resetValue` operand；测试覆盖同步/异步复位、高低有效与嵌套 if。
  - 使能：GRH 的 `kRegister*` 无显式 enable operand，采用数据通路保持（hold）表达使能：当分支未写入时由 `createHoldSlice` 从 Q 端回读并与其他切片 `kConcat`，等价于“未使能时保持”。新增用例：`if (en) r <= d;` 期望 data 路径为 `mux(en, d, q)` 或等价拼接。
- **KR3（内存端口的 enable/mask）**
  - 写口：扩展 `MemoryWriteIntent`/`MemoryBitWriteIntent` 结构，增加 `enable` 字段（默认常量 1）。在 `SeqAlwaysLHSConverter::convert` 记录 `mem[...] <= ...` 或 `mem[...][i] <= b` 时把当前 `currentGuard` 作为 intent.enable；Finalize 时：
    - 整字写生成 `kMemoryWritePort(clk, addr, enable, data)`；
    - 单 bit 写生成 `kMemoryMaskWritePort(clk, addr, enable, shiftedData, shiftedMask)`（复用 `buildShiftedBitValue/buildShiftedMask`），保证 mask 宽度与 memory row 宽度一致。
  - 读口：扩展 `SeqAlwaysConverter::buildMemorySyncRead` 支持可选 enable 输入，进入该表达式时使用 `currentGuard` 作为 `en`（缺省为 1）；从而在 `if (en) rd <= mem[addr];` 中生成 `kMemorySyncReadPort(clk, addr, en)`，避免端口寄存器在分支未触发时也被更新。
  - 合并策略：允许同一 always 块内生成多个写/读端口（每条 intent 一口），先不做 OR 合并；在后续阶段引入端口聚合优化（可选）。
- **KR4（测试与验收）**
  - 新增 `tests/data/elaborate/seq_always.sv` 用例组 `seq_stage19_*`：
    1) if 分支：`if (en) r <= d;`、`if (en) mem[a] <= w;`、`if (en) mem[a][i] <= b;`，断言寄存器 data 路径保留、写口/掩码口的 `en` 为 `en`；
    2) case 分支：`case(sel) 0: mem[a]<=w0; 1: mem[a][i]<=b1; default: ; endcase`，断言各端口 `en` 分别等于相应 `match` 条件，default 不生成写口；
    3) 通配比较：`casez(sel)`/`casex(sel)` 分支匹配构成 `en`，验证 `buildWildcardEquality` 产物被用作 enable；
    4) 复位提取：`if (rst) r<=0; else if (en) r<=d;`，断言寄存器为 `kRegisterRst` 且 `rstPolarity/resetValue` 正确。
  - 在 `tests/elaborate/test_elaborate_seq_always.cpp` 中检查：
    - `kMemorySyncReadPort/kMemoryWritePort/kMemoryMaskWritePort` 的 operand 顺序、`en` 连接与位宽；
    - 寄存器 data 路 `kMux`/`kConcat` 结构与（异/同）步复位 operand；
    - JSON/Graph 快照用于人工抽查（沿用阶段18 artifact 机制）。

> 实现提示
> - 在 `AlwaysConverter` 中新增 guard 栈（例如 `std::vector<grh::Value*> guardStack_`），提供 `pushGuard(grh::Value*)/popGuard()` 与 `currentGuardOrOne()` 辅助；`runWithShadowFrame` 在进入分支前/后配对维护。
> - 所有新建布尔算子均统一宽度为 1；在与父 guard 组合前，对条件表达式进行必要的位宽/符号规范化（如需要用 `kEq` 与常量 1 规约为 1-bit）。
> - 回归现有阶段17/18 用例，确保新增 guard 不影响原有“默认 en=1”的行为；在未命中分支/静态折叠时不产生多余端口或错误连接。

## 阶段20：SeqAlwaysConverter 类处理循环语句
- 目标定位 在 SeqAlwaysConverter 中补齐 for/foreach 循环的处理能力，参考 docs/reference/yosys 的过程展开策略，在 elaboration 阶段对可静态决定的循环进行编译期展开，生成完全静态的语句序列；同时在时序语义下保持非阻塞赋值的“后写覆盖先写”规则，并与阶段19的分支 guard、阶段18的 memory 端口语义自然衔接。
- KR1 (实现策略与范围) 研读 docs/reference/yosys 的循环处理，形成约束清单与实现方式，可新增或扩展一份文档如 docs/elaborate/process-seq-loops.md：
  - 支持 for 与 foreach；禁止 while/do-while。
  - 循环上下限与步长需在 elaboration 期可静态求值；foreach 借助 TypeHelper 给出可枚举的索引/字段列表。
  - 循环体内允许 if/case (沿用阶段19)，允许对 reg/memory 的 NBA 写，允许同步读/写端口的构建。
  - 诊断策略：无法静态确定迭代次数、出现动态 early-exit、或使用阻塞赋值，均报错并给出上下文。
- KR2 (编译期展开与顺序语义) 将循环编译期展开为按源顺序排列的语句副本：
  - for 循环：在进入循环前计算初值/终值/步长；对每个迭代 i，建立新的 shadow 帧并给出迭代变量 i 的常量绑定，展开循环体语句；结束后弹出 shadow。
  - foreach 循环：使用 TypeHelper 列举被遍历的数组/结构体索引序列，逐项展开并绑定迭代变量。
  - 顺序与覆盖：展开后的语句加入到当前 always 块的调度序列，保持“代码后出现的 NBA 覆盖之前同目标的 NBA”的规则；writeBack 与 memory intent 以展开后的线性顺序记录，finalize 按顺序聚合，保证语义等价于源循环逐次执行一遍。
  - guard 继承：每个迭代实例在进入时继承当前 `currentGuard`，循环条件若可静态决定不再额外生成 guard；循环体内的 if/case 继续按阶段19维护 guard，从而为 memory 端口生成正确的 en。
- KR3 (静态 break/continue) 支持静态可预测的 break/continue：
  - 对每个迭代 i，若 break 条件在展开期可被常量折叠为真，则裁剪后续迭代不再生成；若条件依赖运行时值，则报错提示“需要静态可决策的 early-exit”。
  - continue 条件在展开期可为真时，跳过当前迭代中该语句之后的同一迭代余下语句；无法静态决定同样报错。
  - 嵌套循环中，break/continue 的作用域指向最近的可控循环；若使用标签，则需校验标签合法并按标签目标进行裁剪或跳过。
- KR4 (测试与验收) 新增覆盖时序循环的用例组 seq_stage20_*：
  - for 展开与覆盖规则：同一寄存器在多个迭代中被多次 NBA，验证最终 data 路径等价于“最后一次写生效”；包含切片写与整值写的混合。
  - foreach 展开：对 packed/unpacked 数组/结构体字段遍历，验证 TypeHelper 索引顺序与生成的写回拼接顺序一致。
  - break/continue：对可静态判定的 early-exit/跳过场景，验证展开出的迭代数及端口/寄存器写入数量；对不可静态判定场景，验证报错信息。
  - memory 行为：循环内对 memory 的多次写/掩码写，验证生成的 `kMemoryWritePort/kMemoryMaskWritePort` 数量与 `en` 连接等于相应迭代时的 guard；读口在循环体内的生成也应带 `en`。

> 实现提示
> - 复用阶段15的循环展开骨架，在 AlwaysConverter 中抽象一个 `unrollLoop` 帮助函数，支持 comb/seq 两种模式：seq 模式仅改变 writeBack 与 intent 的记录器以及“最后写覆盖”的合并策略。
> - 维护一个「迭代常量绑定」环境，用于把迭代变量替换为常量参与常量折叠；在进入/退出迭代时 push/pop 该绑定，配合 slang/自有常量折叠器判定 break/continue 的静态性。
> - 展开期间一律使用 `runWithShadowFrame` 隔离每次迭代的临时读写视图，避免同一迭代内的读被后续迭代的写污染；最终 writeBack/memory intent 仍以全局线性顺序汇总。
> - 与阶段19的 guard 机制协作：如需对某些循环条件构造 guard，可在进入迭代时把该条件与 `currentGuard` 做与，作为该迭代体内的起始 guard；一般 for/foreach 的迭代谓词若已静态决定，则无需额外 guard。

## 阶段21：扩充寄存器原语及处理方式
- 目标定位 将寄存器生成改为“分化形式”：建图初期仅放置无复位/无使能的 kRegister 占位，顺序路径在 SeqAlwaysConverter 中根据复位/使能语义对寄存器进行“特化”，并补充新的带使能原语。
- 范围影响 涉及 memo 拆分、Reg 构建策略调整、SeqAlwaysConverter 的 finalize 过程、GRH 原语集扩展与测试用例更新；不改变前述 RHS/LHS/Memory 的总体设计。

- KR1（拆分 memo：regMemo 与 memMemo） 将现有“Reg Memo 同时承载寄存器+存储器”的结构拆分：
  - 数据结构
    - regMemo：仅面向需要用寄存器表达的符号，保留 type/width/isSigned/fields/value（Q 口）、drivingBlock、stateOp（寄存器 op）。
    - memMemo：仅面向内存对象，保存 width/row/isSigned、stateOp（kMemory）、以及为读写端口生成服务的附加元数据（例如命名前缀、端口计数器）。
  - 迁移步骤
    1) `collectSignalMemos` 在分类时把 unpacked reg/mem 放入 memMemo，其他 reg 放入 regMemo；
    2) 调整 `ensureRegState/ensureMemState`：前者只处理寄存器，后者只处理存储器；
    3) 更新引用点：RHS 读取寄存器继续从 regMemo.entry.value 取 Q；内存读取/写意图改为通过 memMemo.entry.stateOp；修正 `memo.md`（在本阶段提交中标注需要同步更新）。

- KR2（新增原语：带使能寄存器族） 在 GRH 中补充 3 种带使能的寄存器：
  - kRegisterEn
    - operands：clk, en, d；result：q；attributes：clkPolarity（posedge/negedge）、enLevel（high/low，默认 high）
    - 语义（active-high 默认；enLevel=low 时等价于 `if (!en) hold`）：`always @(clk) if (en) q <= d;`
  - kRegisterEnRst（同步复位）
    - operands：clk, rst, en, rstValue, d；result：q；attributes：clkPolarity, rstPolarity, enLevel
    - 语义：`if (rst==rstPolarity) q<=rstValue; else if (en[enLevel]) q<=d;`
  - kRegisterEnArst（异步复位）
    - operands：clk, rst, en, rstValue, d；result：q；attributes：clkPolarity, rstPolarity（异步边由 rstPolarity 推导）, enLevel
  - 归一化规则
    - enLevel 记录使能极性；复杂条件仍需规约到 1-bit 条件，可在数据路前用 `kNot/kAnd` 等组合逻辑归一化；
    - resetValue 与 d 的位宽必须等于 q 宽度；rst/en 通过 `coerceToCondition` 规约为 1-bit。
  - 规范引用
    - 操作类型总览：`docs/GRH-representation.md:49`
    - kRegisterEn：`docs/GRH-representation.md:361`
    - kRegisterEnRst：`docs/GRH-representation.md:385`
    - kRegisterEnArst：`docs/GRH-representation.md:414`

- KR3（分化式寄存器生成） 基于 KR2 的原语定义，建图初期仅创建基础 `kRegister` 占位，SeqAlwaysConverter 决定最终形态并补全操作数：
  - 初始占位
    - `ensureRegState`：创建 `kRegister`，仅设置 symbol、q result 与 `clkPolarity` 属性（可从 drivingBlock 推断）；暂不连接 `clk/d`，不创建 `*Rst/*ARst/*En*` 变体。
  - finalize 特化流程（面向每个被写的寄存器）
    1) 汇总 writeBack 结果生成“候选 data”SSA；
    2) 提取复位分支（沿用阶段19的 `extractResetBranches`）：若检测到复位，记录 rst 信号、rstPolarity、resetValue 以及是同步/异步类型，并剥离数据路上的复位层；
    3) 提取使能保持：尝试匹配 `mux(en, d, Q)` 或按位拼接后整体等价为“真支为新数据、假支为 Q”的结构；若条件为 `~en` 则归一化为 en + 取反；仅当“全宽保持为 Q”时视为可抽取的 enable；
    4) 选择目标原语：基于是否存在复位（同步/异步）与是否存在使能，确定目标为 `kRegister` / `kRegisterRst` / `kRegisterArst` / `kRegisterEn` / `kRegisterEnRst` / `kRegisterEnArst`；
    5) 迁移/变形：将基础 `kRegister` 替换为目标原语（或在同一 op 上修改 type），连接 `clk/(en)/(rst)/(rstVal)/d` 等 operand；若无法抽取 enable，则保留 `mux(..., Q)` 作为 data 输入的组合逻辑；
    6) 校验与诊断：确认位宽匹配、Q 自引用合法、同一寄存器不会被多个 always 块写入；若发现部分位更新在不同 guard 下且无法归一化为单一 enable，则记录“保持由数据路表达”而非报错。
  - 规范引用
    - kRegister：`docs/GRH-representation.md:282`
    - kRegisterRst：`docs/GRH-representation.md:303`
    - kRegisterArst：`docs/GRH-representation.md:331`
    - kRegisterEn：`docs/GRH-representation.md:361`
    - kRegisterEnRst：`docs/GRH-representation.md:385`
    - kRegisterEnArst：`docs/GRH-representation.md:414`

- KR4（转换与兼容策略） 使能抽取失败或部分位更新的情形：
  - 若数据路径不能整体归一为“else=Q”的保持，保留现有 `kRegister*/mux` 结构，不强制使用 `*En*` 原语；
  - 当存在复位时，优先提取复位再尝试提取使能；允许得到 `kRegisterRst/Arst` + 数据路 mux 的等价表达；
  - 读取路径（RHS）不受影响：一律从 regMemo.entry.value（Q）读取；writeBack 仍然负责拼接多片段写入，SeqAlwaysConverter 只在最终 data 上进行语义提取。

- KR5（测试与验收） 新增与迁移测试：
  - 基础：`seq_stage21_en_reg`（if(en) r<=d;）、`seq_stage21_rst_en_reg`（if(rst) r<=rv; else if(en) r<=d;）
  - 低有效使能：`seq_stage21_nen_reg`（if(!en) r<=q; else r<=d;）验证前置 `kNot`；
  - 部分位更新：`seq_stage21_partial_update`，验证无法整体抽取时保留 mux；复位 + 部分位混合；
  - 兼容性：回归阶段17/18/19/20 的既有用例，确认寄存器/内存语义与端口连接不回退。

> 实现提示
> - 在 `SeqAlwaysConverter::finalizeRegisterWrites` 中插入两个提取器：`extractResetBranches`（已存在）之后新增 `extractEnableHold(data, Q)`；二者返回（剩余 data, enable?, resetCtx?）。
> - 为“原地变形”封装 `mutateRegisterOp(op, targetKind)`，保持 symbol/Q 不变，仅调整 type 与 operands/attributes。
> - 对按位保持的情况，可先尝试将 writeBack 片段合并成“真支为 concat(new_slices)，假支为 Q”的单层 mux，再决定能否抽取 enable。

## 阶段22：将调试打印语句转换为 kDisplay
- 目标定位 把时序过程块中的 `$display/$write/$strobe` 系统任务映射到 GRH 的 `kDisplay` 原语，借助 SeqAlwaysConverter 复用现有 guard/clock 语境；组合过程块暂不支持，需提醒用户。
- 规范基础 参考 `docs/GRH-representation.md:623` 的 `kDisplay` 约束：仅建模有时钟的流程，operands 为 `clk/en/var*`，attributes 包含 `clkPolarity/formatString`。若需要区分 display/write/strobe，可在本阶段评估是否需要新增 `displayKind` attribute 并同步规范。

- KR1（检测入口：SeqAlwaysConverter 中识别系统任务）
  - 在 `SeqAlwaysConverter::visitXYZ`（处理语句调度的入口）增设匹配逻辑：当 `Statement` 是 `ExpressionStatement` 且包裹 `SystemCallExpression`（或 `TimedStatement` 中的 `SystemTask`），且 `symbol.name` 属于 `$display/$write/$strobe` 时进入 display 处理路径。
  - 调用 `slang::FmtHelpers::checkDisplayArgs` 或等价 API 对参数合法性进行一次前置校验；若 slang 已生成诊断则复用，否则在转换器中给出 “display args not supported” 的错误并终止当前 always。
  - 将系统任务节点包装为内部 `DisplayIntent` 结构（记录调用 kind、原始 args、源位置信息），以便后续步骤可以统一处理格式串/变参表达式。
  - 需要覆盖 `$strobe`、`$display`、`$write` 的别名（大小写、带/不带 `$`? slang 正常 AST 中包含 `$`），并确保 future-proof：无法识别的 system task 继续走默认 diag。

- KR2（限定到时序过程块 & warning 策略）
  - 在 `Elaborate` 的 always 块调度阶段，CombAlwaysConverter 遇到 display 系统任务时直接调用诊断接口发出 `warning`：提示“组合逻辑中的 $display 被忽略；GRH 仅保留时序 display”。warning 应附带源位置，便于用户定位。
  - SeqAlwaysConverter 继续支持 display；但若当前 always 块没有有效的 clock（例如 `always @(*)` 被误分类），需要报错提示“kDisplay 仅允许在时序过程块中生成”，并跳过该系统任务。
  - 在 SeqAlwaysConverter 内保持 `currentGuard` 语义：如果 display 出现在 if/case 内部，需把 guard 与 enable 合成（`enable = currentGuard`，若为空则使用常量 1），确保 display 触发条件与寄存器写入一致。
  - 若 display 所在 block 被禁用（例如 `currentGuard` 恒为 0），可以在常量折叠阶段淘汰，但不必强制优化；可在后续 pass 由 DCE 处理。

- KR3（构建 kDisplay Operation）
  - 格式串处理：提取系统任务的第一个参数；若不是字符串字面量，借助 slang constant eval 拿到常量字符串。对于 `$display` 等无格式串但传入表达式列表的情况，按照 slang 的规则将其重写为含 `%0d` 的格式串；这部分可复用 `FmtHelpers::getFormatString`（若存在）或自行实现。
  - 变参表达式求值：为每个格式占位符绑定一个 `SeqRHSConverter` 实例，生成 `Value`；需要确保在 display 中读取 net/reg/memory 时遵循 seq RHS 的访存规则，并保持参数顺序与格式串中的 `%` 顺序一致。
  - operand 布线：`clk` 取自 SeqAlwaysConverter 的上下文（与寄存器写入共用）；`enable` 通过 `ensureGuardValue(currentGuard)` 生成，若 guard 为空则提供常量 1；其余 `var*` 来自上一 bullet 中的转换结果。`clkPolarity` attribute 从 always block 的 `eventControl` 推断（posedge/negedge）；`formatString` attribute 使用上一步生成的字符串；若需要保留 `$write/$strobe` 差异，则在 Operation attribute 中新增 `displayKind`（值为 display/write/strobe）并在 `docs/GRH-representation.md` 同步更新。
  - Graph 插入：在当前模块 `graph` 上调用 `graph.createOperation(kDisplay, operands, attrs)`，并把新节点加入 SeqAlwaysConverter 的“当前语句序列”中，确保 display 相对顺序与源代码一致；必要时在 writeBack finalize 前 flush 一次 pending display 以保持执行顺序。
  - 诊断：当格式串占位符数量与参数不符、存在暂不支持的数据类型（如 strings/unpacked arrays）、或 SeqRHSConverter 报错时，给出清晰的错误并停止生成该 display，避免插入不完整的操作。

- KR4（测试与验收）
  - 构建新的 elaboration 样例 `seq_stage22_display_basic`：含有 posedge always 和 `$display("r=%0d", r);`，验证生成的 `kDisplay` operands/attributes 与预期一致（clk/en/var 顺序正确，formatString 匹配）；测试通过 JSON/文本导出检查 display 节点。
  - 再构建 `seq_stage22_guarded_write`：if 分支内 `$write`，guard 由 enable 控制，验证 `enable` operand 等于 guard 值（例如 combinational expression 生成的 Value）。
  - 构建 `$strobe` 样例并检查 attribute/顺序。对组合 always 中的 display 建立 `comb_stage22_display_warning`，确保 elaboration 输出 warning 但不生成 `kDisplay`。
  - 若新增 `displayKind` attribute，补充规范 & 单元测试断言 attribute 正确；同时回归前序阶段样例，确保未出现额外 `TODO/NYI`。

> 实现提示
> - slang 对 display 族系统任务的 AST 均派生自 `SystemCallStatement`，可通过 `call.kind == SystemTaskKind::Display/Write/Strobe` 分类；`FmtHelpers` 提供的 utilities 可以生成格式串并验证实参。
> - 当前 SeqAlwaysConverter 已有 guard/clock 上下文，可复用 `currentClock`、`clockEdge`、`getCurrentGuardValue()` 等辅助函数，避免重复推导。
> - 对 enable 生成常量 1 时优先复用 `graph.createConstant1(1)` 的缓存，减少重复 Value。
> - 若需要延迟插入 display（例如 `always` 末尾统一 push），可在 converter 内维护一个 `pendingDisplays` 列表，在 `finish()` 时按顺序 emit，确保与写回/寄存器抽象互不干扰。
> - 规范条目：操作类型总览 `docs/GRH-representation.md:49`；寄存器族定义 `docs/GRH-representation.md:282`、`:303`、`:331`、`:361`、`:385`、`:414`。

## 阶段23：识别生成 kAssert 操作
- 目标定位 将顺序 always 块中的断言语句（`assert` / `$fatal` / `$error` 等退化形态）转换为 GRH 的 `kAssert` 原语，保留时钟和条件上下文，组合流程仅告警忽略。
- 规范基础 参考 `docs/GRH-representation.md:639` kAssert 约束：operands 为 `[clk, condition]`，attribute `clkPolarity` 必须匹配时序事件沿。若承载消息，可追加 `message` 字符串（可选）。

- KR1（入口识别与分类）
  - 在 `AlwaysConverter::visitExpressionStatement` 增设断言检测：捕获 `ImmediateAssertionStatement`（`assert`/`assume`/`cover` 仅保留 assert）、`ConcurrentAssertionStatement` 中的 `assert property` 前端若已降级为展开语句也应识别。
  - 捕获 `$fatal/$error/$warning/$asserton` 等系统任务形式，将其归一为 `AssertionIntent`，记录 kind、表达式、可选消息、源位置、是否带 disable iff/clocking block。
  - 对并行/延时（非 #0）断言暂不支持，发出清晰 NYI 诊断。

- KR2（流程限定与 guard 处理）
  - `CombAlwaysConverter` 遇到断言 intent 直接 warning：“组合过程中的 assert 被忽略，GRH 仅建模时序断言”，附源位置。
  - `SeqAlwaysConverter` 仅在存在有效时钟时生成 kAssert：复用 `ensureClockValue()` 与 `clkPolarity` 推导；若缺少 edge-sensitive clock 或 `always @(*)`，报错并跳过。
  - 断言条件需合成 guard：`finalCond = guard ? condition : 1'b1`（或 `guard -> condition`），使用 `coerceToCondition` 规约为 1-bit；disable iff 条件并入 guard。

- KR3（kAssert 节点构建）
  - operands：`clk` 取自当前时序上下文；`condition` 为 KR2 合成结果。
  - attributes：`clkPolarity` 必须设置；若存在消息（如 `$fatal("msg")` 或字符串字面量 side arg），设置 `message`；可选保留 `severity`（fatal/error/warning）以备后端使用。
  - 顺序维护：将 kAssert 插入当前语句序列，保持与寄存器写入/显示语句一致的执行顺序；必要时在 finalize 前 flush pending asserts。
  - 诊断：条件求值失败、非整数条件、消息类型不支持时，报错并跳过生成，避免半成品节点。

- KR4（测试与验收）
  - 样例扩展 `tests/data/elaborate/seq_always.sv`：`seq_stage23_assert_basic`（posedge 上断言 q==d）、`seq_stage23_assert_guard`（if(en) 内断言）、`seq_stage23_assert_property`（assert property @(posedge clk) disable iff(!rst_n) d==$past(d)`）、`comb_stage23_assert_warning`（组合告警）。
  - `tests/elaborate/test_elaborate_seq_always.cpp`：验证 kAssert 数量、operands 绑定（clk + condition 含 guard）、`clkPolarity`/`message` 属性；组合样例仅产生 warning 不生成 kAssert。
  - 回归全量 `ctest --output-on-failure`，确保阶段22/21 等现有断言/显示逻辑无回退。

> 实现提示
> - slang AST：立即断言为 `ImmediateAssertionStatement`，条件表达式可直接访问；`assert property` 会携带 disable/clocking，可从 AST 收集并并入 guard。
> - 若重用 display 流程的 guard/clock 入口，可提取公共 `handleSystemTask` 辅助；coerceToCondition/guardStack 已可直接复用。
> - 对 `$fatal/$error` 输入：无显式条件时可视作 `assert(0)`；若首参为字符串字面量，填充 `message`。
> - 对 cover/assume 目前报 NYI，预留未来扩展。

## 阶段24：添加对 DPI-C 的支持
- 目标定位 把 `import "DPI-C" function void ...` 及其调用映射到 GRH 的 `kDpicImport`、`kDpicCall` 原语，为后续仿真/验证阶段保留对 C 函数交互的完整语义；沿用 SeqAlwaysConverter/guard/clock 体系，禁止无时序上下文的调用。
- 规范基础 `docs/GRH-representation.md:668+`、`:691+` 限定 import 仅支持 void function、参数仅 input/output，call 需要 `[clk, enable, inputs...]` operands 与 `targetImportSymbol`，需严格遵循这些字段。

- KR1（DPI import 声明 -> kDpicImport）
  - 在 Elaborate 初始化模块 memo 时遍历 `slang::SubroutineSymbol`，筛选 `flags.foreignKind == DPIImport` 且 `prototype.isFunction()`，拒绝 task/context/pure/inout/非 void 返回并报清晰诊断。
  - 借助 TypeHelper flatten DPI 形参类型，确保只接受 packed bit 向量；记录 direction/name/bitwidth，必要时对 typedef/enum 先还原基础类型。
  - 在当前模块 graph 顶层创建 `kDpicImport` Operation，`symbol` 采用 SV side 名称 + 可选 scope 防止重名；attributes 写入 argsDirection/argsWidth/argsName。
  - 将 SubroutineSymbol -> Operation 映射存入 `ModuleMemo::dpiImportMap`，用于 call 阶段 lookup；若多次 import 同名函数，检测签名一致否则拒绝。

- KR2（DPI 调用 -> kDpicCall）
  - `SeqAlwaysConverter::visitExpressionStatement`/`visitVoidCall` 识别调用目标为 DPI import（基于上一步 memo），组合/initial/无 clk 块遇到 DPI 调用先 warning/NYI，保持 `kDpicCall` 只能在时序 always 中生成。
  - 按照 import metadata 拆分参数：direction=input 的实参通过 SeqRHSConverter 取 Value，并校验位宽；direction=output 的实参必须是可写 LHS（net/reg/memory slice），使用 writeBack memo 记录“call result -> 实参”回填关系。
  - 生成 `enable = currentGuard 或常量 1`，`clk/clkPolarity` 来自 SeqAlwaysConverter 环境；操作数顺序遵循 `[clk, enable, inArg*]`，results 对应 output args，并在 finalize 阶段把 result Value 写回原 LHS。
  - `targetImportSymbol` attribute 指向匹配的 `kDpicImport`，同时设置 inArgName/outArgName 以保持名称顺序；lookup 失败或模块未 import 直接报错。若函数还有 SystemVerilog side 名字和 C 名字差异，可在 attributes 中额外保留 `cIdentifier` 供后端使用。
  - 诊断路径：实参与 import 方向/数量不符、output 实参非常量/不可寻址、参数类型 flatten 失败、call 出现阻塞等待（如任务 delay）等情况需直接报错，避免生成半成品节点。

- KR3（测试与验收）
  - 新增 `tests/data/elaborate/dpic.sv`：包含至少一个合法 import + posedge always 调用（带 input/output、enable guard）以及一个组合/initial 中的非法调用覆盖 warning/错误；覆盖多位参数与结构体拒绝路径。
  - 在 `tests/elaborate/` 中添加新的 `TEST(ElaborateDpi, ...)`：解析 dpic.sv，断言生成的 `kDpicImport` attributes（direction/width/name）及 `kDpicCall` 的 operands/result/targetImportSymbol/enable；检查 output arguments 通过 writeBack 更新 net/reg。
  - 将 dpic 样例纳入现有 `ctest` 驱动，确保阶段 22/23/SeqAlways pipeline 仍然全部通过；若生成了新的诊断文本，更新 `tests/elaborate/expected_diags.txt`（若存在）保持稳定回归。

> 实现提示
> - slang 中 `ImportExportDecl`/`SubroutineSymbol::getDPIContext()` 描述了 DPI 属性，可直接读取 direction (`FormalArgumentSymbol::direction`) 与 `SubroutineSymbol::getReturnType()`。
> - guard->enable 折叠可复用阶段22/23 的 `ensureGuardValue`；输出写回沿用 writeBack memo，在 `flushSeqStatements` 或专门的 `finalizeDpiCalls` 中一次性提交。
> - 若未来需要支持 `DPI-C task` 或 `context/pure`，在当前阶段留下 `TODO` 某 diag code，便于后续扩展；同理，对于 inout 参数先报 “NYI: DPI inout”。

## 阶段25：添加对 blackbox 的支持
- 目标定位 让 elaborate 能区分“仅声明接口、不含实现”的黑盒模块，并在实例化时生成 GRH `kBlackbox` 节点（含参数/端口元数据），保证上游 net/reg/TypeHelper 信息与后端 netlist 生成功能一致。
- 规范基础 参照 `docs/GRH-representation.md:586+` 对 `kBlackbox` 的 operands/results/attributes 要求；与 `kInstance` 不同，黑盒没有内部 graph，所有端口/参数信息需体现在 attributes 上。

- KR1（识别与 blackboxMemo） 
  - 在模块收集阶段识别 blackbox 声明：优先读取显式标记（如 `(* blackbox *)`、pragma 等），其次可用“模块体为空/仅含端口/参数声明”作为兜底判定；一旦发现过程语句/连续赋值/内部实例化则视为普通模块，不应落入 blackbox 流程。
  - 建立 `blackboxMemo`（moduleName -> InterfaceMeta）：记录端口方向/名称/flatten 后的位宽与顺序、参数名及默认值（常量折叠为字符串），以及原始声明位置用于诊断。
  - blackbox 必须有定义；遇到实例化未知模块且无 blackbox 声明时保持现有错误路径，避免默认为黑盒。

- KR2（实例化生成 kBlackbox） 
  - 实例 elaboration 阶段查 `blackboxMemo`：若命中则跳过内部建图，直接根据 memo 生成 `kBlackbox` Operation。attributes 需填充 `moduleName/instanceName/inputPortName/outputPortName/parameterNames/parameterValues`，operands/results 顺序与 memo flatten 顺序一致。
  - 端口连线：借助 TypeHelper 对实例实参（含结构体/数组/切片）进行 flatten/位宽校验；输入端接到 operands，输出端创建 results 并写回对应 net/reg memo，以便后续驱动能引用到正确的 Value。
  - 参数覆盖：处理 `#(...)` 实例参数，常量折叠为字符串填入 `parameterValues`，未覆盖的使用默认值；不支持的表达式需报错。若端口/参数数量或方向不符，应立即诊断。
  - 支持 generate 内的 blackbox 实例，与普通实例相同纳入层次遍历；确保不会尝试为黑盒生成子 graph，也不会与 `kInstance` 的模块缓存冲突。

- KR3（测试与验收）
  - 新增 `tests/data/elaborate/blackbox.sv`：含一个显式标记为 blackbox 的模块（带参数、向量端口），以及多个实例（直接实例、带参数覆盖、generate 内实例）。期望生成的 `kBlackbox` 节点 attributes/operands/results 与接口一致。
  - 添加负例：同名模块若包含过程语句应被视作普通模块或报错而非黑盒；端口连接缺失/位宽不匹配/未知模块实例化应产生诊断。
  - 在 `tests/elaborate/test_elaborate_blackbox.cpp` 验证 `kBlackbox` 数量、端口名/参数名数组、参数值、操作数绑定和 result 回填；将样例纳入 `ctest`，确保前序阶段用例回归稳定。

> 实现提示
> - blackbox 判定逻辑应集中在模块元信息收集处，避免在实例化阶段重复推导；对 pragma/attribute 的解析可复用 slang 的属性接口。
> - 构造 `kBlackbox` 时复用已有 `kInstance` 的端口 flatten/连接助手，确保 input/output 顺序统一；inout 如未支持需显式报 NYI。
> - parameterValues 建议统一保存为字符串（常量值转十进制或源码文本一致），保持与规范生成语义匹配；在 diag 中打印实例名与模块名便于定位。

## 阶段27：HDLBits dut_162 时序 memory 端口修复
- **背景** `make run_hdlbits_test DUT=162` 报告所有 kMemorySyncReadPort/kMemoryWritePort/kMemoryMaskWritePort 缺失 `clkPolarity`，同时 PHT reset 展开出的写地址是 `signed [31:0]`，与实际 7-bit unsigned 地址不符。
- **KR1（端口属性）** 在 SeqAlwaysConverter 创建 memory 读/写/掩码端口时，无论是否有 `clockPolarityAttr_`，都要显式写入 `clkPolarity`；缺失时直接诊断而非依赖 emit-sv 默认值；回归覆盖 memory 时序端口的属性完整性，确保 warning 清零。
- **KR2（地址归一化）** 根据 kMemory 的行数推导 addrWidth（ceil(log2(rowCount))），在时序 memory 读/写/掩码写入口统一对地址做 zero-extend/截断到 addrWidth 并标记为 unsigned，避免 `signed [31:0]` 膨胀；在 mem attributes 里记录 row/width 便于推导；用 dut_162 或等效用例验收生成的 SV 地址宽度/符号。
- **KR3（回归验证）** 复现 dut_162 流程，确认 emit-sv 无 warning，输出的 memory 端口地址宽度正确；必要时补充单元测试锁定 addrWidth 归一化与 clkPolarity 写入行为。

## 阶段28：改进 kRegister 系列原语
- **目标定位** 统一 GRH 中寄存器族的命名与极性描述，使 enable/复位的极性在规范、实现与测试中都可感知且与时钟描述风格一致。
- **KR1（规范更新：enLevel）** 修改 `docs/GRH-representation.md` 中所有带 enable 的寄存器原语（kRegisterEn/kRegisterEnRst/kRegisterEnArst），增加 `enLevel` attribute，取值 high/low 表示使能极性；明确默认值（若缺省则为 high）与语义（当 enLevel=low 时等价于 `if (!en) hold else load`），并更新操作数/属性表、示例。
- **KR2（命名与极性一致性）** 规范层将 `kRegister*ARst` 重命名为 `kRegister*Arst`，同时把所有寄存器原语中的 `rstLevel` 属性改为 `rstPolarity`（含同步/异步、带/不带 enable 的变体），与 `clkPolarity` 命名对齐；文档中的 type 名称、attribute 表、行内引用与编号需全部替换，保留变更记录供后续 review。
- **KR3（实现与回归迁移）** 将上述规范变更映射到代码与测试：更新 OperationKind/enum/creator 中的类型名与 attribute 键，所有构建寄存器节点的路径（SeqAlwaysConverter 提取复位/使能、emit-sv、Graph 序列化）必须写入新的 `enLevel`/`rstPolarity`，删除或兼容旧字段；同时批量更新单元测试与 JSON dump 的字段名与预期值，保证现有回归（含阶段21 enable 抽取与阶段17/19 复位提取）在新命名下仍然通过。
