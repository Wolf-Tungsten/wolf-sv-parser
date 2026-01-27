# Convert 工作流（动态视角）

> 本文描述 Convert 的运行时流程，强调 Pass 顺序、功能、输入与输出。

## Context Setup（预处理阶段）
- 功能：初始化 `ConvertContext` 与全局缓存。
- 输入：`Compilation`、`RootSymbol`、`ConvertOptions`。
- 输出：`ConvertContext`、top instance 列表、空 `Netlist`。
- 说明：每次转换前清空 `PlanCache`，重置 `PlanTaskQueue`，并从 `RootSymbol` 关联 `Compilation`。

## Pass1: SymbolCollectorPass
- 功能：收集端口/信号/实例信息，形成 ModulePlan 骨架。
- 输入：`InstanceBodySymbol` + ctx。
- 输出形式：`PlanCache[PlanKey].plan`（包含 ports/signals/instances）。

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
