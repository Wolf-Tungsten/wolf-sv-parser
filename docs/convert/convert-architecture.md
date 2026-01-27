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
- 定位：转换期共享状态容器，仅承载指针与配置（不持有资源所有权）。
- 字段总览（类型 -> 语义）：
  - `compilation`: `const slang::ast::Compilation*` -> 访问全局类型/语义信息。
  - `root`: `const slang::ast::RootSymbol*` -> 访问 `topInstances` 等根级符号。
  - `options`: `ConvertOptions` -> 转换选项的值拷贝。
  - `diagnostics`: `ConvertDiagnostics*` -> 诊断收集器指针。
  - `logger`: `ConvertLogger*` -> 日志收集器指针。
  - `planCache`: `PlanCache*` -> 模块计划缓存指针。
  - `planQueue`: `PlanTaskQueue*` -> 模块任务队列指针。
- 字段详解（总分结构递归）：
  - `compilation`：
    - 作用：提供类型系统与语义查询入口。
    - 建立：`ConvertDriver::convert` 中从 `root.getCompilation()` 取得。
    - 输出：供 Pass1~Pass8 读取，不在 Context 内写回。
  - `root`：
    - 作用：根符号视图，承载顶层实例列表。
    - 建立：`ConvertDriver::convert` 传入。
    - 输出：供顶层任务投递与 diagnostics 定位使用。
  - `options`（值对象）：
    - 作用：控制诊断与日志策略。
    - 建立：`ConvertDriver` 构造时保存的 `options_` 在 convert 中拷贝。
    - 子成员（字段 -> 类型 -> 含义）：
      - `abortOnError`: `bool` -> 诊断 error 是否触发 `ConvertAbort`。
      - `enableLogging`: `bool` -> 是否启用 `ConvertLogger`。
      - `logLevel`: `ConvertLogLevel` -> 日志等级过滤阈值。
  - `diagnostics`（指针）：
    - 作用：集中收集 todo/error/warn。
    - 建立：指向 `ConvertDriver::diagnostics_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `messages_`: `std::vector<ConvertDiagnostic>` -> 诊断条目列表。
      - `hasError_`: `bool` -> 是否已记录 error。
      - `onError_`: `std::function<void()>` -> error 回调（可抛出 `ConvertAbort`）。
    - 输出：供 CLI 输出与测试断言使用。
  - `logger`（指针）：
    - 作用：统一日志出口，支持等级与 tag 过滤。
    - 建立：指向 `ConvertDriver::logger_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `enabled_`: `bool` -> 是否启用日志。
      - `level_`: `ConvertLogLevel` -> 当前日志等级阈值。
      - `tags_`: `std::unordered_set<std::string>` -> 允许的 tag 集合。
      - `sink_`: `Sink`（`std::function<void(const ConvertLogEvent&)>`）-> 日志输出回调。
    - 输出：供调试/诊断流输出。
  - `planCache`（指针）：
    - 作用：模块计划与中间产物缓存，负责去重与状态管理。
    - 建立：指向 `ConvertDriver::planCache_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `entries_`: `std::unordered_map<PlanKey, PlanEntry>` -> 计划状态与产物集合。
      - `mutex_`: `std::mutex` -> 并行访问保护。
    - 输出：供 Pass1~Pass7 写入与读取 `ModulePlan`/`PlanArtifacts`。
  - `planQueue`（指针）：
    - 作用：模块计划任务队列，驱动模块级流水线。
    - 建立：指向 `ConvertDriver::planQueue_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `queue_`: `std::deque<PlanKey>` -> 待处理任务。
      - `closed_`: `bool` -> 队列关闭标记。
      - `mutex_`: `std::mutex` -> 并行访问保护。
    - 输出：供主流程投递与 drain。
- 生命周期与初始化：
  - 构造位置：`ConvertDriver::convert` 栈上临时构造。
  - 初始化顺序：先 `planCache.clear()` 与 `planQueue.reset()`，再填充 Context 字段。

### PlanEntry（核心结构）
- 定位：模块计划的“状态 + 产物”核心单元，承载一个模块从计划到写回的生命周期。
- 字段总览（类型 -> 语义）：
  - `status`: `PlanStatus` -> 计划处理阶段状态。
  - `plan`: `std::optional<ModulePlan>` -> 模块计划（Pass1~Pass3 写入）。
  - `artifacts`: `PlanArtifacts` -> Pass4~Pass7 的中间产物占位。
- 字段详解：
  - `status`：
    - 作用：描述计划生命周期状态（`Pending/Planning/Done/Failed`）。
    - 建立：`PlanCache::tryClaim` 置为 `Planning`；`storePlan` 置为 `Done`；
      `markFailed` 置为 `Failed`。
    - 输出：作为并行去重与状态检查的依据。
  - `plan`：
    - 作用：保存 `ModulePlan`（端口/信号/实例/读写等计划信息）。
    - 建立：`ModulePlanner::plan` 生成后由 `PlanCache::storePlan` 写入。
    - 输出：供 Pass2~Pass8 使用；失败时置空。
  - `artifacts`：
    - 作用：保存 Pass4~Pass7 的中间结果容器。
    - 建立：在 `PlanEntry` 构造时默认存在；内容由 Pass4/Pass7 写入。
    - 子成员（字段 -> 类型 -> 含义）：
      - `loweringPlan`: `std::optional<LoweringPlan>` -> Pass4/Pass5/Pass6 产物。
      - `writeBackPlan`: `std::optional<WriteBackPlan>` -> Pass7 产物。
    - 输出：供 Pass8 使用。
- 关联结构（围绕 PlanEntry 的容器/索引）：
  - `PlanKey`：
    - 作用：唯一标识参数特化模块（`body + paramSignature`）。
    - 建立：Pass1 发现实例体时生成；`paramSignature` 由非 localparam 的参数名和值序列化而来。
    - 输出：作为 `PlanCache` 的 key 访问 `PlanEntry`。
  - `PlanCache`：
    - 作用：`PlanKey -> PlanEntry` 的并发缓存与去重容器。
    - 建立：`ConvertDriver` 持有，`ConvertContext` 仅指针引用。
    - 输出：提供 `tryClaim/storePlan/set*Plan` 等接口给各 pass 使用。
  - `PlanArtifacts`：
    - 作用：`PlanEntry` 内部的中间产物容器。
    - 建立：随 `PlanEntry` 默认构造，内容由 Pass4/Pass7 写入。
    - 输出：提供 Pass8 的输入。

### ModulePlan
- 定位：模块级静态计划，集中记录端口/信号/实例/读写关系等“长期稳定事实”。
- 字段总览（类型 -> 语义）：
  - `body`: `const slang::ast::InstanceBodySymbol*` -> 模块实例体指针。
  - `symbolTable`: `PlanSymbolTable` -> 模块内符号驻留表。
  - `moduleSymbol`: `PlanSymbolId` -> 模块名（或回退名）的 intern 结果。
  - `ports`: `std::vector<PortInfo>` -> 端口骨架记录。
  - `signals`: `std::vector<SignalInfo>` -> 信号骨架记录。
  - `rwOps`: `std::vector<RWOp>` -> 读写访问记录。
  - `memPorts`: `std::vector<MemoryPortInfo>` -> memory 端口记录。
  - `instances`: `std::vector<InstanceInfo>` -> 子实例记录。
- 字段详解（总分结构递归）：
  - `body`：
    - 作用：提供 AST 成员遍历入口。
    - 建立：`ModulePlanner::plan` 绑定。
    - 输出：供 Pass1~Pass3 遍历 AST。
  - `symbolTable`：
    - 作用：统一管理模块内符号名与 `PlanSymbolId`。
    - 建立：Pass1 在收集端口/信号/实例时调用 `intern`。
    - 输出：供后续 pass 查找/反查符号名。
  - `moduleSymbol`：
    - 作用：模块名索引（用于 Graph 命名与诊断标注）。
    - 建立：Pass1 根据 `InstanceBodySymbol` 与 `DefinitionSymbol` 生成。
    - 输出：供 Pass8/日志使用。
  - `ports`（`PortInfo`）：
    - 作用：承载端口方向与类型信息。
    - 建立：Pass1 创建 `symbol/direction`，Pass2 补齐 `width/isSigned`。
    - 子成员（字段 -> 含义）：
      - `symbol`: `PlanSymbolId` -> 端口名。
      - `direction`: `PortDirection` -> in/out/inout。
      - `width`: `int32_t` -> 位宽（Pass2 填充）。
      - `isSigned`: `bool` -> 是否有符号（Pass2 填充）。
      - `inoutSymbol`: `optional<InoutBinding>` -> `__in/__out/__oe` 派生名。
  - `signals`（`SignalInfo`）：
    - 作用：承载信号类型、位宽与维度信息。
    - 建立：Pass1 创建 `symbol/kind`，Pass2 补齐宽度/维度。
    - 子成员（字段 -> 含义）：
      - `symbol`: `PlanSymbolId` -> 信号名。
      - `kind`: `SignalKind` -> Net/Variable/Memory/Port。
      - `width`: `int32_t` -> 位宽（Pass2 填充）。
      - `isSigned`: `bool` -> 是否有符号（Pass2 填充）。
      - `memoryRows`: `int64_t` -> fixed unpacked 行数乘积。
      - `packedDims`: `std::vector<int32_t>` -> packed 维度（外到内）。
      - `unpackedDims`: `std::vector<int32_t>` -> unpacked 维度（外到内）。
  - `rwOps`（`RWOp`）：下方章节有详细介绍
    - 作用：记录一次读/写访问（target/domain/isWrite）。
    - 建立：Pass3 填充。
    - 输出：供 Pass4~Pass7 读写归因。
  - `memPorts`（`MemoryPortInfo`）：
    - 作用：记录 memory 端口访问属性。
    - 建立：Pass3 在识别 memory 访问时生成。
    - 输出：供 Pass6 细化 memory 端口（详见 MemoryPortInfo 章节）。
  - `instances`（`InstanceInfo`）：
    - 作用：记录子实例关系与黑盒属性。
    - 建立：Pass1 扫描 `InstanceSymbol/GenerateBlock*` 填充。
    - 输出：供 Pass4~Pass7 构建实例图与下游队列（详见 InstanceInfo 章节）。
- 设计意图：
  - `ModulePlan` 放在顶层是为了保存“长期稳定事实”（符号、宽度、读写关系等），
    后续 Pass 都需要它作为共同基准。
  - `LoweringPlan/WriteBackPlan` 被包在 `PlanArtifacts` 中是为了标识“阶段性产物”，
    可按需生成、缓存或释放，避免与稳定骨架耦合。
  - 这种分层明确生命周期边界，便于并行/增量执行时隔离写入冲突与内存管理。

### RWOp
- 定位：`ModulePlan.rwOps` 的单条读写记录，描述“哪个信号在什么控制域发生读/写”。
- 字段总览（类型 -> 语义）：
  - `target`: `SignalId` -> 目标信号索引（指向 `ModulePlan.signals`）。
  - `domain`: `ControlDomain` -> 控制域语义（comb/seq/latch/unknown）。
  - `isWrite`: `bool` -> 是否为写访问（false 表示读）。
- 字段详解：
  - `target`：
    - 作用：绑定具体信号条目，避免重复字符串解析。
    - 建立：RWAnalyzer 通过 `PlanSymbolId -> SignalId` 映射解析。
    - 输出：供 Pass4~Pass7 做读写归因、域内排序与冲突检测。
  - `domain`：
    - 作用：标注读写发生的控制域，用于区分 combinational 与 sequential 语义。
    - 建立：RWAnalyzer 依据 `ProceduralBlockSymbol` 与 timing control 分类。
    - 输出：影响后续写回策略、memory 端口同步推断。
  - `isWrite`：
    - 作用：区分读/写访问；与 `target/domain` 共同构成去重键。
    - 建立：RWAnalyzer 在遍历 LHS/RHS 时决定。
    - 输出：指导后续 Pass 在生成 Value/Op 时使用正确的数据流方向。
- 生成与使用约定：
  - 生成：Pass3 负责填充并去重；同一信号可在不同域有多条记录。
  - 使用：Pass4~Pass7 以 `rwOps` 作为读写关系输入，不直接回扫 AST。

### MemoryPortInfo
- 定位：`ModulePlan.memPorts` 的单条 memory 端口记录，描述“某个 memory 在某类访问形态下需要的端口能力”。
- 字段总览（类型 -> 语义）：
  - `memory`: `SignalId` -> memory 信号索引（指向 `ModulePlan.signals`）。
  - `isRead`: `bool` -> 是否存在读访问。
  - `isWrite`: `bool` -> 是否存在写访问。
  - `isMasked`: `bool` -> 是否带字节/位写掩码（当前版本恒为 false，预留）。
  - `isSync`: `bool` -> 是否同步端口（顺序域访问为 true）。
  - `hasReset`: `bool` -> 是否带显式 reset 语义（当前版本恒为 false，预留）。
- 字段详解：
  - `memory`：
    - 作用：绑定到具体 memory 信号条目，避免字符串查找。
    - 建立：RWAnalyzer 通过 `PlanSymbolId -> SignalId` 映射解析。
    - 输出：供 Pass6 选择 memory lowering 策略与端口模板。
  - `isRead`/`isWrite`：
    - 作用：描述端口读写能力；`true/false` 组合表达只读/只写/读写口。
    - 建立：RWAnalyzer 在读/写访问被识别时记录。
    - 输出：影响 Pass6 生成的端口类型与连接逻辑。
  - `isMasked`：
    - 作用：标识写端口是否需要掩码能力（如 byte-enable）。
    - 建立：当前实现未推断掩码访问，固定为 false。
    - 输出：保留给后续支持掩码写的 lowering。
  - `isSync`：
    - 作用：标识端口是否为同步访问口。
    - 建立：RWAnalyzer 依据访问控制域推断（顺序域 = true）。
    - 输出：影响 Pass6 选择 sync/async memory 端口形态。
  - `hasReset`：
    - 作用：标识 memory 访问是否依赖显式 reset。
    - 建立：当前实现未推断 reset 语义，固定为 false。
    - 输出：预留给后续带 reset 端口的 lowering。
- 生成与去重约定：
  - 生成：Pass3 在识别到 `SignalInfo.memoryRows > 0` 的读/写访问时创建。
  - 去重：以 `memory + isRead + isWrite + isMasked + isSync + hasReset` 作为 key 去重，
    允许同一 memory 在不同访问形态下拥有多个端口记录。
  - 使用：Pass6 以 `memPorts` 为输入，不回扫 AST；同一 memory 的多端口合并策略在 Pass6 中处理。

### InstanceInfo
- 定位：`ModulePlan.instances` 的单条子实例记录，描述“当前模块里包含哪些实例、它们指向哪个 module 体、是否黑盒”。
- 字段总览（类型 -> 语义）：
  - `instanceSymbol`: `PlanSymbolId` -> 实例名（空则回退数组名）。
  - `moduleSymbol`: `PlanSymbolId` -> 模块定义名（空则回退实例名）。
  - `isBlackbox`: `bool` -> 是否视作黑盒实例。
  - `parameters`: `std::vector<InstanceParameter>` -> 仅黑盒实例保留的参数绑定列表。
- 字段详解：
  - `instanceSymbol`：
    - 作用：给子实例一个稳定名字索引，便于日志/Graph 组装与任务追踪。
    - 建立：Pass1 从 `InstanceSymbol.name` 获取；为空则回退 `getArrayName()`。
    - 输出：用于下游图构建的实例命名与诊断信息拼接。
  - `moduleSymbol`：
    - 作用：指向实例所绑定的模块定义名，作为 PlanKey 与 Graph 目标模块名的来源。
    - 建立：Pass1 优先取 `InstanceBodySymbol.getDefinition().name`；为空则回退实例名。
    - 输出：用于生成子模块的 `PlanKey` 与后续实例连接的目标模块名。
  - `isBlackbox`：
    - 作用：标记该实例是否只有接口而无实现。
    - 建立：Pass1 调用黑盒判定逻辑：若 body 无实现成员则视为 blackbox；
      若显式标注 blackbox 但仍有实现，记录 error 并按普通模块处理。
    - 输出：影响 Pass4~Pass7 是否展开实例内部逻辑。
  - `parameters`：
    - 作用：保留 blackbox 的参数化信息，供 GRH 记录/导出。
    - 建立：Pass1 从 `InstanceBodySymbol.getParameters()` 读取（跳过 localparam），
      将参数名 intern 成 `PlanSymbolId` 并记录序列化后的值。
    - 输出：供黑盒实例生成时填充 `parameterNames/parameterValues`。
- 生成与使用约定：
  - 生成：Pass1 在扫描 `InstanceSymbol/InstanceArraySymbol/GenerateBlock*` 时创建记录。
  - 任务发现：每条 `InstanceInfo` 关联一个子模块 `PlanKey`，由 Pass1 递归投递到队列。
  - 使用：Pass4~Pass7 消费 `instances` 构建实例层级与连接关系，不回扫 AST。

### Pass1: SymbolCollector 数据结构落位
- 入口：`ModulePlanner::plan`，创建 `ModulePlan` 并绑定 `body`/`moduleSymbol`。
- 目标：建立模块的“符号骨架”，为后续类型解析与读写分析提供稳定的符号索引。
- 数据结构与作用：
  - `ModulePlan`：
    - 作用：模块级静态计划容器，集中持有 ports/signals/instances 的骨架记录。
    - 建立：`ModulePlanner::plan` 创建并绑定 `body` 与 `moduleSymbol`。
    - 输出：作为 Pass2~Pass8 的共享输入与写入目标。
  - `PlanSymbolTable`：
    - 作用：模块内符号驻留表，统一管理模块名/端口名/信号名/实例名的 intern 结果，
      以 `PlanSymbolId` 作为轻量索引，避免重复分配与字符串拷贝。
    - 建立：Pass1 在收集端口/信号/实例时调用 `intern` 填充。
    - 输出：`ModulePlan.symbolTable` 被后续 pass 用于 name 查找与反向映射。
  - `PortInfo`：
    - 作用：记录端口方向与命名，为端口建模与 inout 拆分做准备。
    - 建立：来自 `InstanceBodySymbol::getPortList()` 的 `PortSymbol`。
    - 输出：填充 `symbol/direction`；inout 生成 `__in/__out/__oe` 派生名。
  - `SignalInfo`：
    - 作用：记录信号“存在性”与初始分类，为后续类型与读写分析提供入口。
    - 建立：扫描 `InstanceBodySymbol::members()` 中的 `NetSymbol/VariableSymbol`。
    - 输出：填充 `symbol/kind`，宽度/维度留给 Pass2。
  - `InstanceInfo`：
    - 作用：记录实例关系（instanceSymbol/moduleSymbol/blackbox），用于层次保留与实例化。
    - 建立：扫描 `InstanceSymbol/InstanceArraySymbol/GenerateBlockSymbol/GenerateBlockArraySymbol`。
    - 输出：追加到 `ModulePlan.instances`，同时触发任务发现。
  - `PlanKey` + `PlanTaskQueue`：
    - 作用：描述“待计划子模块”的任务单元，并驱动后续模块计划流程。
    - 建立：Pass1 遍历到子实例体时生成 `PlanKey{body}` 并投递。
    - 输出：`PlanTaskQueue` 中新增任务，供主流程 drain。
- 约束：
  - Pass1 仅构建符号级骨架，不生成 Value/Op。
  - 不支持的端口类型与匿名符号会记录诊断并跳过。

### Pass2: TypeResolver 数据结构落位
- 入口：`TypeResolverPass::resolve`，在 Pass1 之后就地更新 `ModulePlan`。
- 目标：把 slang 类型系统的静态信息写回 `ModulePlan`，为后续 RW/Lowering 准备稳定的宽度与维度描述。
- 数据结构与作用：
  - `TypeResolution`（内部临时结构）：
    - 作用：承载单个端口/信号的类型分析结果，避免在 `ModulePlan` 上进行半成品写入。
    - 建立：`analyzePortType`/`analyzeSignalType` 在解析 `slang::ast::Type` 时生成。
    - 输出：写入 `PortInfo` 或 `SignalInfo` 的 `width/isSigned/packedDims/unpackedDims/memoryRows`。
  - `PortIndexMap`（局部索引表，`std::vector<PortId>`）：
    - 作用：把 `PlanSymbolId` 映射到 `ModulePlan.ports` 索引，避免 O(n^2) 查找。
    - 建立：`TypeResolverPass::resolve` 启动时遍历 `ModulePlan.ports` 构建。
    - 输出：仅在 Pass2 内部使用，不写回 `ModulePlan`。
  - `SignalIndexMap`（局部索引表，`std::vector<SignalId>`）：
    - 作用：把 `PlanSymbolId` 映射到 `ModulePlan.signals` 索引。
    - 建立：`TypeResolverPass::resolve` 启动时遍历 `ModulePlan.signals` 构建。
    - 输出：仅在 Pass2 内部使用，不写回 `ModulePlan`。
  - `PortInfo`：
    - 作用：承载端口最终的位宽/符号属性，供后续端口 Value/Op 构造使用。
    - 建立：Pass1 创建并填充 `symbol/direction`，Pass2 补齐 `width/isSigned`。
    - 输出：inout 的 `__in/__out/__oe` 复用原端口的宽度与符号信息。
  - `SignalInfo`：
    - 作用：承载信号最终的位宽/符号属性与维度信息，作为 memory 判定与信号建模的输入。
    - 建立：Pass1 创建并填充 `symbol/kind`，Pass2 补齐 `width/isSigned`，
      并收集 `packedDims/unpackedDims`。
    - 输出：`memoryRows` 保存 fixed unpacked 维度的乘积；
      `packedDims/unpackedDims` 以外到内顺序记录维度大小。

### Pass3: RWAnalyzer 数据结构落位
- 入口：`RWAnalyzerPass::analyze`，在 Pass2 之后就地更新 `ModulePlan`。
- 数据结构与作用：
  - `RWOp`：
    - 作用：记录一次读/写访问（目标 `SignalId`、`ControlDomain`、`isWrite`），用于后续
      读写归因、控制域判定与写回策略选择。
    - 建立：RWAnalyzer 在遍历过程块/连续赋值时生成。
    - 输出：追加到 `ModulePlan.rwOps`，允许同一信号在不同域出现多条记录。
  - `MemoryPortInfo`：
    - 作用：记录 memory 的访问端口属性（读/写/同步/掩码/复位），为 Pass6 细化端口提供输入。
    - 建立：RWAnalyzer 在识别到 `SignalInfo.memoryRows > 0` 的读写访问时生成。
    - 输出：追加到 `ModulePlan.memPorts`，`isSync` 由控制域推断。
  - `ControlDomain`：
    - 作用：统一描述访问的控制域语义（comb/seq/latch/unknown）。
    - 建立：根据 `ProceduralBlockSymbol` 与 timing control 分类。
    - 输出：嵌入 `RWOp`，并影响 memory 端口 `isSync`。
  - `SignalIndexMap`（局部索引表）：
    - 作用：`PlanSymbolId -> SignalId` 映射，快速把 AST 符号落到 `ModulePlan.signals`。
    - 建立：RWAnalyzer 启动时扫描 `ModulePlan.signals` 构建。
    - 输出：仅用于 Pass3 内部，不写回 `ModulePlan`。
  - `RWOpDedup` / `MemPortDedup`（局部去重集合）：
    - 作用：对 `(signal, domain, isWrite)` 与 `(memory, flags)` 去重，避免重复记录。
    - 建立：RWAnalyzer 生命周期内维护。
    - 输出：仅用于 Pass3 内部。
  - `RWAnalyzerState`（内部工作态）：
    - 作用：封装 `ModulePlan` 指针、索引表与去重集合，统一管理读写记录入口。
    - 建立：`RWAnalyzerPass::analyze` 创建一次并贯穿整个模块分析。
    - 输出：通过 `recordRead/recordWrite` 写入 `rwOps` 与 `memPorts`。
  - `RWVisitor`（AST 访问器）：
    - 作用：在语句树中区分 LHS/RHS，并将 NamedValue/HierarchicalValue 记为读或写。
    - 建立：每个过程块/连续赋值构造一个 visitor，绑定当前 `ControlDomain`。
    - 输出：调用 `RWAnalyzerState` 写入访问记录。
  - `ControlDomainClassifier`（分类逻辑）：
    - 作用：把 `ProceduralBlockSymbol` 映射到 control domain，用于访问语义标注。
    - 建立：基于 `procedureKind` 与 timing control（edge-sensitive 或 level-sensitive）。
    - 输出：提供 `ControlDomain` 给 RWVisitor 与 memory 端口推断。

### PlanSymbolTable 与索引化结构
- `PlanSymbolTable`：模块内符号驻留表，`string_view -> PlanSymbolId`。
- 索引化主键：`PlanSymbolId/SignalId/PortId`；主表以 `std::vector` 顺序存储。
- 访问模式：只追加写入，避免中途删除；依据 `InstanceBodySymbol` 成员数量预留容量。
- 输出：`ModulePlan.symbolTable` 被后续 pass 用于 name 查找与反向映射。

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
