# Slang AST 到 GRH 转换 OKR

> 备注：JSON 序列化仅用于人工检查，自动化测试通过直接访问 GRH 内部结构完成。
> 每次开始工作前，回顾 docs/overview.md 和 docs/GRH-representation.md
> 每次开始工作前，回顾前序阶段的完成情况（阅读docs/elaborate/phase*-report.md），在前序工作基础上开展
> 需要与 slang 交互时，阅读 external/slang 中源码
> 可参考 docs/reference/slang-netlist docs/reference/verilator docs/reference/yosys 实现中的算法

## 阶段1：GRH 数据结构与序列化
- **Objective** 严格遵循 `docs/GRH-representation.md` 定义构建 GRH 核心数据结构，并完成稳定的序列化基础。
  - KR1 完成 GRH 节点、边类型与属性的设计评审，形成对照 `docs/reference/yosys`、`docs/reference/slang-netlist` 的比对表，并确认不触及 `docs/GRH-representation.md`。
  - KR2 在 `include/grh` 内落地核心结构与内存布局，单元测试覆盖常见节点（连线、网、寄存器、存储器、断言），校验字段与语义与规范一致。
  - KR3 实现 JSON 序列化/反序列化供人工核查，并在单元测试中通过 GRH API 验证结构一致性，若需调整规范需先提交变更审批；`grh-tests` 运行时需将 round-trip 结果以 pretty JSON 写入 `${CMAKE_BINARY_DIR}/artifacts/grh_stage1_demo.json` 供阶段评审手动检视。

## 阶段2：Elaborate 框架雏形
- **Objective** 建立从 slang AST 读取到 GRH 草图输出的最小可行流程。
  - KR1 引入 `slang` 前端加载管线，能解析 RTL 顶层并提供 AST 访问器。
  - KR2 搭建 `elaborate` 管线骨架，生成节点占位并可导出 JSON 以便人工抽查。
  - KR3 提供诊断接口，未实现特性触发 `TODO`/`NYI` 告警并通过 `ctest` 的 smoke 测试。

## 阶段3：模块层次结构建模
- **Objective** 实现层次化模块的解析，将实例化关系映射到 GRH。
  - KR1 支持模块声明、端口解析，并与 `verilator` 分层策略对照审查。
  - KR2 实现 TypeHelper，能够将 packed/unpacked 结构体、数组 flatten 成 GRH Value 支持的形式，并提供解析能力（符号对应哪些位？）
  - KR3 生成层次化 GRH 图（graph），实现 kInstance 操作
  - KR4 为模块层次添加 2 个以上的回归样例，覆盖嵌套实例与参数化实例。

## 阶段4：参数化模块处理
- **Objective** GRH 不保留参数化模块支持，需要为每个参数化实例生成独立的 Graph
  - KR1 增强阶段3的层次支持，对参数化后变得独特的模块独立建图，而非参数化的模块仍然能复用
  - KR2 支持 generate 语句和模块实例化数组的处理
  - KR3 构建覆盖上述特性的测试样例

## 阶段5：net 和 reg 解析
- **Objective** 对于每个模块，解析用户定义的 net/reg/logic 信号，形成两个独立的 Slang AST Symbol memo 表
  - KR1 在 elaborate 类中增加两个备忘录结构，分别存储解析为 net 和 reg 声明的符号与类型信息
  - KR2 实现对 slang AST 中 logic/net/reg 声明的遍历
  - KR3 判定每个 logic/net/reg 最终属于 net 还是属于 reg，判断依据：被 assign/always_comb/always@(*) 驱动的为 net，被 always@(posedge/negedge)、always_ff 驱动的为 reg，双重驱动则报错
  - KR4 将判定后的符号与类型信息存入对应备忘录
  - KR5 构建覆盖上述特性的测试样例，对备忘录内容进行 peek 测试

## 阶段6：复杂类型的 net 和 reg 解析
- **Objective** 在阶段5的基础上，增强 net 和 reg 的解析
  - KR1 统一术语命名，将原来的 wire 和 reg 改为 net 和 reg 表述，包括修改代码和文档
  - KR2 增强 TypeHelper，使得 net 和 reg 的解析可以处理 packed/unpacked 结构体和数组，请注意，GRH对于结构体和数组的处理方法是按照 Systemverilog 规范进行 flatten，形成一个大的 Value，之后通过 kSlice* 读取，通过 kConcat 写回
  - KR3 阶段5的实现中，会产生 Module body elaboration incomplete，这是不必要的，net 和 reg 只解析线网和变量即可，GRH不支持跨Graph的信号访问

## 阶段7：由 net 和 reg memo 创建 Value 和 Operation 
- **Objective** 把阶段5-6生成的 memo 投影到可落地的 GRH Value/Operation，为后续连线与驱动填充留好锚点。
  - KR1 针对每个 net memo 条目，调用 TypeHelper 的 flatten 结果生成一组 GRH `Value`，并在 memo 中回填 `Value` 句柄（含 bit 位宽、层级来源），保证后续通过符号即可定位到唯一的 `Value`。
  - KR2 对 reg memo 条目，结合前序驱动分类信息，为时序寄存器与存储器分别创建 `kRegister*`、`kMemory` Operation，占位的数据/时钟/复位 operand 可以先用 stub，memo 中需记录 `entry -> Operation` 的映射及关键属性（宽度、初值来源等）。
  - KR3 补充覆盖标量/结构体/数组、net 与 reg 混合、不同 always 类型的回归样例；测试需输出人类可读的 Value/Operation 映射（JSON 或文本），以便人工校验。

## 阶段8：创建 RHSConverter 基类 
- **Objective** 实现 RHSConverter 类，用于将 slang AST 中的右值表达式转换为 GRH Operation 和 Value 链并插入到当前模块的 GRH 图中。
  - KR1 设计 RHSConverter 类的接口和基本结构，确保其能够接受 slang AST 表达式节点、当前 GRH 图和上下文信息（memo）作为输入。
  - KR2 RHSConverter 应当是一个可扩展的基类，后续会派生成 CombRHSConverter 和 SeqRHSConverter 两个子类，分别处理组合逻辑和时序逻辑的右值表达式转换。
  - KR3 RHSConverter 类应全面覆盖 slang AST 中的右值表达式类型，生成对应的 GRH Operation 和 Value 链。
  - KR4 RHSConverter 返回的是一个 GRH Value，表示表达式的计算结果。
  - KR5 当 RHSConverter 需要对 net 或者 reg 进行读取时，通过 memo 查找对应的 GRH Value。
  - KR6 构建覆盖上述特性的测试样例，验证 RHSConverter 的正确性和完整性。

## 阶段9：创建 CombRHSConverter 派生类
- **Objective** 实现继承自 RHSConverter，专门处理组合逻辑右值表达式的 CombRHSConverter
  - KR1 支持对结构体、数组乃至更复杂嵌套的 net 和 reg 读取时生成 kSlice*
  - KR2 支持对 mem 读取时生成 kMemoryAsyncReadPort
  - KR3 支持对简单 net 和 reg 的 concat 和 slice 读取
  - KR4 构建自动测试样例，同时生成人工可读的json

## 阶段10：设计实现 Value 写回 memo 数据结构
- **Objective** 为解析连续赋值、过程块准备数据结构。由于 sv 语法在 LHS 支持表达式，若直接写回 net 和 reg memo 中的 Value 会破坏 GRH 要求的 SSA 特性，因此elaborate中准备一个写回 memo 数据结构，记录这些赋值，在所有连续赋值和过程块解析完成后，由写回 memo 最终生成对 memo 中 Value 的 concat 或直接赋值
  - KR1 阅读理解目前 net / reg memo 的数据结构，设计新的 writeBack memo，并将三种 memo 的数据结构写成 memo.md 文档供后人参考
  - KR2 设计的数据结构既要考虑解析连续赋值和过程块的效率，也要考虑最终 finalize 成实际 Value 赋值的效率
  - KR3 将设计的数据结构进行代码实现。

## 阶段11：支持解析纯组合逻辑的 assign 连续赋值
- **Objective** 添加对纯组合逻辑的 assign，基于现有的 CombRHSConverter 和 memo 实现
  - KR1 调用 CombRHSConverter 实现 assign 中 RHS 的解析，生成 Value
  - KR2 对 LHS 进行解析，添加必要的 kSlice* 操作，写入 writeBack memo
  - KR3 构建测试样例，覆盖复杂数据类型，形成人类可读的 json 输出供检查

## 阶段12：创建 CombAlwaysConverter 类
- **Objective** 为解析组合逻辑过程块创建 CombAlwaysConverter 类
  - KR1 CombAlwaysConverter 用于 always_comb 和 always@(*) 类型过程块的解析，在 elaborate 过程中由 processCombAlways 调用
  - KR2 CombAlwaysConverter 维护解析组合逻辑阻塞赋值语义、块作用域内的 memo，例如 a = 1；b = a；时，b 应该得到 1
  - KR3 结合 KR2 的 memo，CombAlwaysConverter 需要派生自己的 CombAlwaysRHSConverter 和 自定义的 LHS 处理逻辑
  - KR4 CombAlwaysConverter 在转换结束后，将创建的 Operation,Value 写回 graph，最终的赋值结果写回 writeback memo
  - KR5 CombAlwaysConverter 预留之后支持组合逻辑 if/case/loop 的 TODO 入口

## 阶段13：CombAlwaysConverter 添加 if/case 支持
- **Objective** 使得 CombAlwaysConverter 支持 if/case 等可综合的分支语句
  - KR1 调研 docs/reference/yosys 中处理 if 和 case 语句的算法，形成可参考的文档
  - KR2 在 CombAlwaysConverter 中实现上述算法，创建 kMux 结构，如果需要可以增强 CombAlwaysLHSConverter
  - KR3 只支持可综合的 if/case 结构，对于产生 latch 的情况报错处理
  - KR4 创建足够复杂的测试案例

## 阶段14：CombAlwaysConverter 添加静态分支分析能力
- **Objective** CombAlwaysConverter 对于其中可静态分析的分支结构提供支持
  - KR1 对于可静态分析的分支，不产生 kMux
  - KR2 能够处理静态与动态分支嵌套的情况
  - KR3 创建测试样例

## 阶段15：CombAlwaysConverter 添加循环展开能力
- **Objective** CombAlwaysConverter 支持 for/foreach 循环语句
  - KR1 参考 docs/reference/yosys 的实现方法，形成循环语句处理算法规划文档
  - KR2 对循环进行编译时展开，生成静态结构
  - KR3 支持静态可预测的 break 和 continue 语法
  - KR4 创建测试样例
  - KR5 扩展 `isCombProceduralBlock`，对 `always @(a or b ...)` 等仅包含电平敏感信号列表的过程块视作组合逻辑，保证循环展开前的 block 分类一致
