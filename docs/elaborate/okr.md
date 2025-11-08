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
- **Objective** 在阶段 6 的基础上，根据 net 和 reg memo 创建 Value
  - KR1 net memo 中的信号直接创建 Value，扩展 net memo 记录信号和 Value 的关系
  - KR2 reg memo 中的信号需解析类型和赋值的 always 块特点，生成 kRegister* 和 kMemory，并扩展 reg memo 记录 memo entry 和 kRegister*、kMemory 的关系，kRegister* 和 kMemory 的 operand 可以先留空，留待后续步骤解析
  - KR3 创建测试样例，提供人类可读的检查输出