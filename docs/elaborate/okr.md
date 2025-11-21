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

## 阶段16：创建 SeqAlwaysConverter 类
- **Objective** 为解析时序逻辑过程块创建 SeqAlwaysConverter 类、SeqAlwaysLHSConverter 类
  - KR1 调研 docs/reference/yosys 中判定时序逻辑过程块的方法，并扩展当前 elaborate 流程添加 processSeqAlways 实现集成
  - KR2 参考当前 CombAlwaysConverter 的实现，提取一个基类 AlwaysConverter，之后 CombAlwaysConverter 和 SeqAlwaysConverter 均由该基类派生，该基类包含 if/case/loop 处理的公共部分，最终块级的 finalize 阶段由子类按行为定义
  - KR3 SeqAlwaysConverter 配合 SeqAlwaysLHSConverter 类解析非阻塞赋值语句，时序always块中不允许出现阻塞赋值，报错。
  - KR3 SeqAlwaysConverter 的 finalize 阶段将绑定 kRegister 的输入 value，创建 kMemory 的读写口，请你规划这个流程，但具体是实现标记为 TODO

## 阶段17：SeqAlwaysConverter 类处理寄存器非阻塞赋值
- **Objective** SeqAlwaysConverter 在遇到寄存器非阻塞赋值时，能够正确设置 kRegister 的 clk、rst、d 操作数
  - KR1 当时序 always 块中出现对 kRegister 的非阻塞赋值时，要正确设置被赋值 kRegister 的所有操作数
  - KR2 核查 kRegister 的类型、attribute正确（有无复位？时钟复位极性？），操作数尚未被设置？不满足条件则报错
  - KR3 支持同一个 always 块中对同一寄存器分段不重叠的非阻塞赋值，生成正确的 kConcat
  - KR4 对于同一个 always 块中对同一寄存器重复或者分段重叠的非阻塞赋值，后来者覆盖前者
  - KR5 对于同一个 always 块中切片赋值但又没有完全赋值的寄存器，未操作的片段应当保留原值，通过将 q 的 slice 和 d 的 slice 拼接实现

## 阶段18：SeqAlwaysConverter 类处理memory读写口
- **Objective** 参考 docs/reference/yosys 实现方法，将对 memory 的非阻塞赋值和读取建模成读写口
  - KR1 将时序 always 块中的 memory 读取建模为 kMemorySyncReadPort，绑定 memory、clk、addr、en，如果没有明确识别的 en 信号则将 en 连接到 1
  - KR2 将时序 always 块中，单次完整的 memory 写入建模为 kMemoryWritePort，绑定 memory、clk、addr、en、data，如果没有明确识别的 en 信号则将 en 连接到 1
  - KR3 将时序 always 块中，对 memory 单bit的写入，记录下来，在 always 结束时根据语义合成 kMemoryMaskWritePort
  - KR4 创建测试样例

## 阶段19：SeqAlwaysConverter 类处理if/case语句
- **Objective** 在 SeqAlwaysConverter 中添加分支语句处理能力
  - KR1 能够解析 if/case 语句
  - KR2 解析结果形成 kRegister* 的复位、使能信号
  - KR3 解析结果形成 memory 读写口的使能和 mask
  - KR4 创建测试样例

## 阶段20：SeqAlwaysConverter 类处理循环语句
- **Objective** 在 SeqAlwaysConverter 中添加循环语句处理能力
  - KR1 参考 docs/reference/yosys 的实现方法，形成循环语句
  - KR2 对循环进行编译时展开，生成静态结构
  - KR3 支持静态可预测的 break 和 continue 语法
  - KR4 创建测试样例

## 阶段21：扩充寄存器原语及处理方式
- **Objective** 将寄存器生成的方式改造为“分化形式”，并添加新的寄存器原语
  - KR1 目前实现中，建图初期将 kRegister 和 kMemory 放在了一个 memo 中，请分离成 regMemo 和 memMemo
  - KR2 改为建图初期只产生 kRegister，在 SeqAlwaysConverter 中对 kRegister 的类型和操作数进行补充
  - KR3 目前 GRH 表示中缺少带有 Enable 的寄存器，请补充 kRegisterEn、kRegisterEnRst、kRegisterEnARst
  - KR4 添加对新型寄存器的支持
  - KR5 补充测试样例，并确保已有测试样例正确回归。

## 阶段22：将调试打印语句转换为 kDisplay
- **Objective** 将 slang AST 中的 $display/$write/$strobe 调试打印语句转换为 GRH 中的 kDisplay 操作
  - KR1 在 SeqAlwaysConverter  中，识别 slang AST 中的 $display/$write/$strobe 调试打印语句
  - KR2 只支持时序过程块中的调试打印语句，组合逻辑过程块中的调试打印语句忽略并 warning
  - KR3 将这些调试打印语句转换为 GRH 中的 kDisplay 操作，正确设置属性，连接操作数，并插入到当前模块的 GRH 图中
  - KR4 创建测试样例，验证调试打印语句的正确转换和插入。

## 阶段23：识别生成 kAssert 操作
- **Objective** 将顺序 always 块中的 `assert`（及 `$fatal/$error` 等退化形态）转换为 GRH `kAssert` 原语，使后续验证/仿真阶段可感知断言语义。
  - KR1（入口识别）在 `AlwaysConverter` 中捕获 `ImmediateAssertionStatement`（`assert (...)`/`assert #0` 等）以及 `$assert/$fatal/$error` 系统任务，形成统一的 `AssertionIntent`；若为并行/延时断言暂不支持，需报清晰诊断。
  - KR2（流程限定）仅在 `SeqAlwaysConverter` 中生成 `kAssert`，组合过程块遇到断言统一 warning 并忽略；对缺少时钟/edge 的时序块发出错误，保持与阶段22 相同的约束。
  - KR3（kAssert 构建）根据 `docs/GRH-representation.md:639+` 要求，创建操作数 `[clk, condition]`，`condition` 需结合当前 guard（assert 触发区间）进行 `cond = guard -> expr` 的规约；attribute `clkPolarity` 复用时钟推导结果，必要时追加 `message`（若来自 `$error("msg")`）。对 `assert property` 解析到的 disable/clock 合并 guard。
  - KR4（测试 & 回归）在 `tests/data/elaborate/seq_always.sv` 添加断言样例：基本 assert、带 enable guard、`assert property`、组合断言 warning；`tests/elaborate/test_elaborate_seq_always.cpp` 校验生成的 `kAssert` 数量、操作数、属性，确保阶段17/22 等既有测试稳定。

## 阶段24：添加对 DPI-C 的支持
- **Objective** 添加对sv通过DPI调用外部函数的支持
  - KR1 在elaborate过程中，识别DPI导入的函数声明，将 import DPI 函数映射为GRH中的kDpicImport操作
  - KR2 在elaborate过程中，识别DPI调用语句，将DPI调用映射为GRH中的kDpicCall操作，正确设置操作数和属性
  - KR3 创建测试样例dpic.sv，验证DPI导入和调用的正确转换
