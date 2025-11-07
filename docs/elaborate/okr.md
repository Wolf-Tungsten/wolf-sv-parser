# Slang AST 到 GRH 转换 OKR

> 备注：JSON 序列化仅用于人工检查，自动化测试通过直接访问 GRH 内部结构完成。

## 阶段1：GRH 数据结构与序列化
- **Objective** 严格遵循 `docs/GRH-representation.md` 定义构建 GRH 核心数据结构，并完成稳定的序列化基础。
  - KR1 完成 GRH 节点、边类型与属性的设计评审，形成对照 `docs/reference/yosys`、`docs/reference/slang-netlist` 的比对表，并确认不触及 `docs/GRH-representation.md`。
  - KR2 在 `include/grh` 内落地核心结构与内存布局，单元测试覆盖常见节点（连线、网、寄存器、存储器、断言），校验字段与语义与规范一致。
  - KR3 实现 JSON 序列化/反序列化供人工核查，并在单元测试中通过 GRH API 验证结构一致性，若需调整规范需先提交变更审批。

## 阶段2：Elaborate 框架雏形
- **Objective** 建立从 slang AST 读取到 GRH 草图输出的最小可行流程。
  - KR1 引入 `slang` 前端加载管线，能解析 RTL 顶层并提供 AST 访问器。
  - KR2 搭建 `elaborate` 管线骨架，生成节点占位并可导出 JSON 以便人工抽查。
  - KR3 提供诊断接口，未实现特性触发 `TODO`/`NYI` 告警并通过 `ctest` 的 smoke 测试。

## 阶段3：模块层次结构建模
- **Objective** 实现层次化模块的解析，将实例化关系映射到 GRH。
  - KR1 支持模块声明、端口、参数、生成块的遍历，并与 `verilator` 分层策略对照审查。
  - KR2 生成层次化 GRH 图（graph），实现 kInstance 操作
  - KR3 为模块层次添加 2 个以上的回归样例，覆盖嵌套实例与参数化实例。

## 阶段4：连续赋值 `assign`
- **Objective** 将连续赋值语句映射为 GRH 中的组合驱动关系。
  - KR1 复用 `slang-netlist` 的表达式遍历策略，完成运算符节点映射表。
  - KR2 为 bit-slice、拼接、条件运算实现驱动边生成并通过位宽一致性检查。
  - KR3 在 `ctest` 中新增包含多重 `assign` 的组合逻辑基线用例，通过 GRH 访问器校验驱动关系。

## 阶段5：组合行为 `always_comb` / `always @(*)`
- **Objective** 支持组合过程块的 elaboration，确保对驱动集合的精确追踪。
  - KR1 借鉴 `docs/reference/slang-netlist-data-flow-rationale.md` 的数据流分析，为过程块建立状态收集器。
  - KR2 覆盖阻塞赋值、初始条件与敏感列表自动推导，生成对应 GRH 节点。
  - KR3 用 3 个行为描述样例验证驱动覆盖度与无重复驱动，基于 GRH 内部结构断言。

## 阶段6：组合流程控制展开
- **Objective** 对组合逻辑中的 if/case/loop 进行静态展开，消除控制依赖。
  - KR1 实现控制依赖图与条件边，并参考 `verilator` 控制流展开策略校验。
  - KR2 对 case/unique case、for/while 循环进行展开或展开失败诊断。
  - KR3 引入覆盖 if/case/loop 的回归集，通过结构对比验证 GRH 结果匹配预期。

## 阶段7：时序过程 `always_ff` / `always`
- **Objective** 支持同步过程块，捕获时序边界与敏感列表。
  - KR1 解析时钟与复位条件，借鉴 `yosys` 时序单元建模记录触发边。
  - KR2 支持非阻塞赋值驱动，确保过程内控制结构沿用阶段6的展开能力。
  - KR3 针对双时钟与同步复位场景构建测试样例，检查 GRH 边界节点与触发条件。

## 阶段8：寄存器识别
- **Objective** 从时序过程抽取寄存器并在 GRH 中生成 `kRegister*` 节点。
  - KR1 定义寄存器节点属性（位宽、复位值、驱动源）并与 `yosys` 寄存器抽象对照。
  - KR2 在 elaborate 收尾阶段聚合寄存器信息，避免重复实例化。
  - KR3 增加包含多寄存器与位切片的回归用例，通过 GRH 数据接口校验寄存器属性。

## 阶段9：存储器识别
- **Objective** 捕获 `mem` 语义，生成 `kMemory` 节点及读写端口。
  - KR1 设计内存节点 schema（深度、宽度、端口数量），参考 `yosys`、`verilator` 内存建模。
  - KR2 支持多读多写端口解析，识别同步/异步读写模式。
  - KR3 使用真实项目片段验证内存 elaboration，通过 GRH 结构检视端口连线拓扑。

## 阶段10：仿真输出 `$display`
- **Objective** 将 `$display/$write` 等仿真输出映射为 GRH 中的 `kDisplay` 节点。
  - KR1 建立消息节点格式与参数捕获机制，支持静态与动态参数列表。
  - KR2 在 elaboration 过程中标记其触发上下文，确保与时序/组合节点正确关联。
  - KR3 添加含条件输出的测试，通过 GRH 节点属性验证触发条件与输出内容。

## 阶段11：仿真终止 `$finish/$error`
- **Objective** 识别仿真终止类语句，生成 `kAssert` 节点并捕获触发条件。
  - KR1 复用阶段6的条件展开，记录断言表达式及 fail/pass 边。
  - KR2 实现严重级别分类（finish/error/fatal）并体现在节点属性中。
  - KR3 构建覆盖不同终止语句的测试用例，通过 GRH 链路验证触发源可追溯。

## 阶段12：DPI 交互
- **Objective** 完成 DPI 函数的识别与 GRH 建模，生成 `kDpicImport` / `kDpicCall`。
  - KR1 建立 DPI 导入表，与 `verilator` DPI 绑定流程对照确认参数/返回值处理。
  - KR2 在 elaboration 中区分 import、export、task/function 调用并建立调用边。
  - KR3 编写含 DPI 调用的端到端 demo，基于 GRH 结构比对调用链符合预期。
