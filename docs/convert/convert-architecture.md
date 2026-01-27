# Convert 架构（静态视角）

> 本文聚焦数据结构与并行策略，描述 Convert 的静态组织方式。

## 目标与边界
- 输入：slang AST（`slang::ast::*`），由前端构建的 `Compilation` 与 `RootSymbol` 提供。
- 输出：GRH IR（`grh::ir::Netlist` / `Graph` / `Value` / `Operation`）。
- 职责：把已解析并类型化的 AST 转为结构化网表，并输出可定位的诊断信息。
- 非职责：覆盖所有 SV 特性、完整时序/综合语义证明。

## 设计原则
- 分阶段：先收集与分析，再生成 IR，避免边扫边写导致状态纠缠。
- 以 slang 类型系统为源：宽度、符号、packed/unpacked 维度、参数值均从 `slang::ast::Type` 与常量求值获取。
- 显式的数据模型：使用 Plan/Info 结构保存信号、端口、读写关系与控制域，降低隐式 memo。
- SSA 与四态：GRH 以 SSA 表达，四态逻辑语义保持从 slang 到 GRH 一致。
- 模块粒度隔离：每个 `InstanceBodySymbol` 形成独立的 ModulePlan 与 Graph，顶层与子实例一致处理。
- 层次保留：不做全局扁平化，实例关系通过 kInstance 维护。
- 可控日志：提供统一的调试日志接口，默认静默，可按级别与模块启用。

## 核心数据结构
### ConvertContext
- 共享状态容器，贯穿整个转换流程，仅承载指针与配置，不持有资源所有权。
- 字段：`compilation`、`root`、`options`、`diagnostics`、`logger`、`planCache`、`planQueue`。
- 构造位置：`ConvertDriver::convert` 栈上临时构造，指向 driver 内部的 diagnostics/logger/cache/queue。
- 初始化顺序：`planCache.clear()` 与 `planQueue.reset()` 后再填充字段，避免残留状态。
- 选项与诊断：
  - `options` 为值拷贝（`abortOnError`、`enableLogging`、`logLevel`）。
  - `abortOnError` 为真时，`ConvertDiagnostics` 触发 `ConvertAbort` 终止流程。
  - `ConvertLogger` 仅在 `enableLogging` 时启用，并受 level/tag 过滤与 sink 约束。
- 当前实现：Context 完成组装并注入给 `ModulePlanner`/`GraphAssembler` 入口，后续 pass 仍待接入。

### PlanKey / PlanCache / PlanArtifacts
- `PlanKey`：`body + paramSignature`，唯一标识参数特化模块。
- `PlanCache`：`PlanKey -> PlanEntry{status, plan, artifacts}`。
- 访问接口：支持 set/get 与 `with*` 回调访问，回调在锁内执行以避免拷贝。
- `PlanArtifacts`：`loweringPlan` + `writeBackPlan`。

### ModulePlan
- 模块级静态计划，结构化记录端口/信号/读写关系等。
- 字段：`ports`、`signals`、`rwOps`、`memPorts`、`instances`。
- 端口（`PortInfo`）：
  - `name` 为端口符号名；inout 以 `inout` 绑定结构记录 `in/out/oe` 三个派生名（`__in/__out/__oe`）。
  - `direction` 仅由 `PortSymbol::direction` 决定，宽度/签名在 Pass2 才填。
- 信号（`SignalInfo`）：
  - Pass1 只填 `name` 与 `kind`（Net/Variable），宽度/维度/记号在后续 pass 完成。
  - 匿名 net/variable 直接跳过并记录 warn。
- 实例（`InstanceInfo`）：
  - `instanceName` 使用实例名，若为空则回退到 `getArrayName()`。
  - `moduleName` 优先使用 `DefinitionSymbol::name`，为空则回退到实例名。
  - `isBlackbox` 依据实现存在性判断（当前实现：无实现即 blackbox；若显式标注 blackbox 但仍有实现则报错并按普通模块处理）。

### Pass1: SymbolCollector 数据结构落位
- 入口：`ModulePlanner::plan`，创建 `ModulePlan` 并绑定 `body`/`symbol`。
- 收集来源：
  - 端口：`InstanceBodySymbol::getPortList()` 中的 `PortSymbol`。
  - 信号：`InstanceBodySymbol::members()` 中的 `NetSymbol`/`VariableSymbol`。
  - 实例：`InstanceSymbol`/`InstanceArraySymbol`/`GenerateBlockSymbol`/`GenerateBlockArraySymbol`。
- 任务发现：遍历到子实例时，生成 `PlanKey{body}` 并投递到 `PlanTaskQueue`。
- PlanSymbolTable：
  - 统一 intern 模块名、端口名、信号名与实例名，避免重复分配。
  - Pass1 不生成任何 Value/Op，仅构建符号级骨架。

### PlanSymbolTable 与索引化结构
- `PlanSymbolTable`：模块内符号驻留表，`string_view -> PlanSymbolId`。
- 索引化主键：`PlanSymbolId/SignalId/PortId`；主表以 `std::vector` 顺序存储。
- 访问模式：只追加写入，避免中途删除；依据 `InstanceBodySymbol` 成员数量预留容量。

### LoweringPlan / WriteBackPlan
- `LoweringPlan`：临时 Value/Op 描述、依赖关系与临时命名。
- `WriteBackPlan`：合并后的写回序列与 guard/mux 决策。

### Diagnostics / Logger
- `ConvertDiagnostics`：统一收集 todo/error/warn，保留源码位置信息。
- `ConvertLogger`：level/tag 过滤的可控日志接口。

## Pass 与数据结构关系图
```
Context Setup
  |
  v
PlanCache (PlanKey -> PlanEntry{plan, artifacts})
  |
  +--> Pass1: SymbolCollectorPass -> ModulePlan(ports/signals/instances)
  |                               -> store in PlanCache[PlanKey].plan
  |
  +--> Pass2: TypeResolverPass    -> update ModulePlan (width/signed/dims)
  |
  +--> Pass3: RWAnalyzerPass      -> update ModulePlan (rwOps/memPorts)
  |
  +--> Pass4: ExprLowererPass     -> LoweringPlan
  |                               -> PlanCache[PlanKey].artifacts.loweringPlan
  |
  +--> Pass5: StmtLowererPass     -> refine LoweringPlan (guards/write intents)
  |
  +--> Pass6: MemoryPortLowererPass -> refine LoweringPlan (mem ports)
  |
  +--> Pass7: WriteBackPass       -> WriteBackPlan
  |                               -> PlanCache[PlanKey].artifacts.writeBackPlan
  |
  +--> Pass8: GraphAssemblyPass   -> Graph (GRH Op/Value) -> Netlist
```

## 并行化策略（以模块为粒度）
### 并行边界
- 一个参数特化模块对应一个任务流水线。
- Pass1~Pass7 可并行执行；Pass8 串行写入 `Netlist`。

### 任务与调度
- 任务 key：`PlanKey = body + paramSignature`。
- 调度：固定线程池 + 任务队列；主线程负责投递与汇总。
- 任务发现：在 Pass1 遍历实例时，将子模块的 `PlanKey` 投递到队列。

### 去重与同步
- `PlanCache` 负责去重，保证同一 `PlanKey` 只处理一次。
- `PlanTaskQueue` 负责任务分发，支持队列关闭以收敛并行阶段。

### 数据一致性
- 并行阶段只写 `PlanCache/PlanArtifacts`，不改 `Netlist`。
- `GraphAssemblyPass` 串行落地 Graph 与 alias，避免并发写冲突。

### 诊断与日志
- 诊断：线程本地缓存，最终由主线程合并。
- 日志：`ConvertLogger` 统一输出，避免多线程交错。

## GRH 输出契约
- 端口与信号宽度、符号属性完全基于 slang 类型系统。
- memory 仅使用精简后的读写端口集合（`kMemoryAsyncReadPort` / `kMemorySyncReadPort` / `kMemorySyncReadPortRst` / `kMemorySyncReadPortArst` / `kMemoryWritePort` / `kMemoryMaskWritePort`）。
- 读端口复位仅作用于读寄存器，写端口无复位语义。
- 四态逻辑语义保持一致，常量允许 x/z。
- Netlist 允许多个顶层模块，Convert 标记 `topInstances` 形成 topGraphs 列表。

## 代码骨架落位
- `include/convert.hpp`：Convert 核心数据结构与接口声明。
- `src/convert.cpp`：Convert 骨架实现与默认行为。
- `CMakeLists.txt`：新增 `convert` 静态库目标。
