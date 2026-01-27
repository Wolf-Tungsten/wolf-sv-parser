# Convert 工作流（动态视角）

> 本文描述 Convert 的运行时流程，强调 Pass 顺序、功能、输入与输出。

## 概念速览（本文内使用）
- `RootSymbol`：slang AST 的设计根节点，提供 `topInstances` 与 `Compilation`。
- `ConvertDriver`/`ConvertOptions`：转换入口与配置，负责创建 ctx 并控制日志/诊断策略。
- `ConvertContext`（简称 ctx）：贯穿转换流程的共享状态，包含 diagnostics/logger/cache/queue 等指针。
- `PlanKey`：`InstanceBodySymbol + paramSignature` 的组合，用于唯一标识参数特化模块；`paramSignature` 为参数求值后的序列化标识。
- `PlanCache`：`PlanKey -> PlanEntry{plan, artifacts}` 的缓存容器。
- `PlanEntry`：`PlanCache` 内的单条记录，包含状态、`ModulePlan` 与 `PlanArtifacts`。
- `PlanTaskQueue`：`PlanKey` 队列，用于调度待处理模块。
- `ModulePlan`：模块级静态计划，存放 `ports`/`signals`/`instances` 等骨架数据。
- `PortInfo`/`SignalInfo`/`InstanceInfo`：`ModulePlan` 内的端口/信号/实例记录。
- `PlanArtifacts`：与 `ModulePlan` 配套的中间产物容器（Lowering/WriteBack 等）。
- `LoweringPlan`/`WriteBackPlan`：Pass4/Pass7 产出并缓存到 `PlanArtifacts` 的中间计划。
- `Netlist`/`Graph`：Convert 的最终 GRH 输出；`topGraphs` 记录顶层 Graph 名称。
- `GRH`：内部中间表示（Graph-based IR），Convert 的目标输出。
- `InstanceBodySymbol`：slang AST 的实例体（模块/接口/程序体），Pass1 的主要输入。
- `DefinitionSymbol`：slang AST 的模块/接口/程序定义符号，用于提供模块名。
- `ConvertDiagnostics`/`ConvertLogger`：诊断与日志收集器，贯穿 ctx 生命周期。
- `PortSymbol`/`NetSymbol`/`VariableSymbol`：slang AST 中的端口与信号符号。
- `InstanceSymbol`/`InstanceArraySymbol`：slang AST 中的实例与实例数组。
- `GenerateBlockSymbol`/`GenerateBlockArraySymbol`：slang AST 中的 generate block 与其数组。

## Context Setup（预处理阶段）
- 功能：初始化 `ConvertContext` 与全局缓存，准备空 `Netlist`。
- 输入：`RootSymbol` 与 `ConvertDriver` 内的 `ConvertOptions`（构造时已配置日志/诊断）。
- 输出：空 `Netlist` 与已填充的 `ConvertContext`（指向 diagnostics/logger/cache/queue）。
- 运行步骤：
  - `ConvertDriver` 构造时：设置 `logger` 的 level；若 `enableLogging` 为真则启用 logger；
    若 `abortOnError` 为真则设置 diagnostics 的 `onError` 回调抛出 `ConvertAbort`。
  - `ConvertDriver::convert` 开始：创建空 `Netlist`，并执行 `planCache.clear()`、
    `planQueue.reset()` 清理历史状态。
  - 组装 `ConvertContext`：`compilation = &root.getCompilation()`，`root = &root`，
    `options` 拷贝，`diagnostics/logger/planCache/planQueue` 绑定到 driver 内部实例。
- 当前实现已完成 Context Setup 与 Pass1，仍返回空 `Netlist`，后续 Pass 逻辑待接入。

## Pass1: SymbolCollectorPass
- 功能：收集端口/信号/实例信息，形成 ModulePlan 骨架，并发现子模块任务。
- 输入：`InstanceBodySymbol` + ctx。
- 输出形式：`PlanCache[PlanKey].plan`（包含 ports/signals/instances）。
- 运行步骤：
  - 初始化 `ModulePlan`：设置 `plan.body`，根据 `body.name`/`definition.name` 生成 `plan.symbol`。
  - 端口收集：
    - 遍历 `body.getPortList()`，只处理 `PortSymbol`，并记录 `PortInfo{name,direction}`。
    - inout 通过 `PortInfo.inout` 记录 `__in/__out/__oe` 名称；`Ref` 方向与匿名/空端口报告 error。
    - `MultiPortSymbol`/`InterfacePortSymbol` 当前视为不支持并记录 error。
  - 信号收集：
    - 扫描 `body.members()` 中的 `NetSymbol`/`VariableSymbol`。
    - Pass1 仅填 `SignalInfo{name,kind}`，宽度/维度由 Pass2 补齐。
    - 匿名符号跳过并记录 warn。
  - 实例收集与任务发现：
    - 扫描 `InstanceSymbol`/`InstanceArraySymbol`/`GenerateBlockSymbol`/`GenerateBlockArraySymbol`。
    - 记录 `InstanceInfo{instanceName,moduleName,isBlackbox}`。
    - 为每个子实例体投递 `PlanKey{body}` 到 `PlanTaskQueue`。
- 当前实现由 `ConvertDriver::convert` 先投递顶层实例，再 drain 队列并写入 `PlanCache`。

## Pass2: TypeResolverPass
- 功能：计算位宽、签名、packed/unpacked 维度与 memory 行数。
- 输入：`ModulePlan` + `slang::ast::Type` + ctx。
- 输出形式：原地更新 `ModulePlan`。

## Pass3: RWAnalyzerPass
- 功能：建立读写关系与控制域语义。
- 输入：`ModulePlan` + 过程块/连续赋值 AST + ctx。
- 输出形式：原地更新 `ModulePlan.rwOps`/`ModulePlan.memPorts`。

## Pass4: ExprLowererPass
- 功能：RHS 表达式降级为临时 Value/Op 描述。
- 输入：`ModulePlan` + 表达式 AST + ctx。
- 输出形式：`PlanCache[PlanKey].artifacts.loweringPlan`。

## Pass5: StmtLowererPass
- 功能：语句与控制流降级，形成 guard/mux 与写回意图。
- 输入：`ModulePlan` + 语句 AST + `LoweringPlan` + ctx。
- 输出形式：更新 `LoweringPlan`。

## Pass6: MemoryPortLowererPass
- 功能：细化 memory 读写端口的 Lowering 描述。
- 输入：`ModulePlan.memPorts` + `LoweringPlan` + ctx。
- 输出形式：更新 `LoweringPlan`。

## Pass7: WriteBackPass
- 功能：合并写回序列与 guard/mux 决策。
- 输入：`ModulePlan` + `LoweringPlan` + ctx。
- 输出形式：`PlanCache[PlanKey].artifacts.writeBackPlan`。

## Pass8: GraphAssemblyPass
- 功能：落地 GRH Graph，绑定端口并生成实例化。
- 输入：`ModulePlan` + `LoweringPlan` + `WriteBackPlan` + `Netlist` + ctx。
- 输出形式：`Netlist` 中新增 `Graph` 与 topGraphs 标记。

## 输出
- 返回 `Netlist` 给 emit/transform 等下游，诊断信息交由 CLI 输出。
