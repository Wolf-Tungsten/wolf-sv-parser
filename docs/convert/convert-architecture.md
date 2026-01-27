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
- 总览：
  - 定位：描述 Convert 的核心模型与共享状态，定义模块计划、符号索引与访问关系的稳定形态。
  - 结构清单：PlanKey、ConvertContext、PlanEntry、ModulePlan、PlanSymbolTable、AccessSite、
    RWOp、MemoryPortInfo、InstanceInfo、LoweringPlan/WriteBackPlan、Diagnostics/Logger。

### PlanKey
- 总览：
  - 定位：模块计划的唯一标识，区分不同参数特化实例。
  - 角色：作为 PlanCache/PlanTaskQueue 的 key，保证特化模块不会合并。
- 字段总览（字段 -> 类型 -> 含义）：
  - `body`: `const slang::ast::InstanceBodySymbol*` -> 模块实例体指针。
  - `paramSignature`: `std::string` -> 参数序列化签名（非 localparam）。
- 字段详解：
  - `body`：
    - 作用：指向具体模块体，提供稳定索引。
    - 建立：Pass1 扫描实例体时填入。
    - 输出：作为 PlanCache 与 PlanTaskQueue 的 key。
  - `paramSignature`：
    - 作用：区分参数特化实例，避免不同参数值被合并。
    - 建立：Pass1 序列化参数名和值生成。
    - 输出：参与 PlanKey 哈希与相等判断。

### ConvertContext
- 总览：
  - 定位：转换期共享状态容器，仅承载指针与配置（不持有资源所有权）。
  - 角色：为 Pass1~Pass8 提供编译期信息与缓存/队列入口。
- 字段总览（字段 -> 类型 -> 含义）：
  - `compilation`: `const slang::ast::Compilation*` -> 访问全局类型/语义信息。
  - `root`: `const slang::ast::RootSymbol*` -> 访问 `topInstances` 等根级符号。
  - `options`: `ConvertOptions` -> 转换选项的值拷贝。
  - `diagnostics`: `ConvertDiagnostics*` -> 诊断收集器指针。
  - `logger`: `ConvertLogger*` -> 日志收集器指针。
  - `planCache`: `PlanCache*` -> 模块计划缓存指针。
  - `planQueue`: `PlanTaskQueue*` -> 模块任务队列指针。
- 字段详解：
  - `compilation`：
    - 作用：提供类型系统与语义查询入口。
    - 建立：`ConvertDriver::convert` 中从 `root.getCompilation()` 取得。
    - 输出：供 Pass1~Pass8 读取，不在 Context 内写回。
  - `root`：
    - 作用：根符号视图，承载顶层实例列表。
    - 建立：`ConvertDriver::convert` 传入。
    - 输出：供顶层任务投递与 diagnostics 定位使用。
  - `options`：
    - 作用：控制诊断与日志策略。
    - 建立：`ConvertDriver` 构造时保存的 `options_` 在 convert 中拷贝。
    - 子成员（字段 -> 类型 -> 含义）：
      - `abortOnError`: `bool` -> 诊断 error 是否触发 `ConvertAbort`。
      - `enableLogging`: `bool` -> 是否启用 `ConvertLogger`。
      - `logLevel`: `ConvertLogLevel` -> 日志等级过滤阈值。
  - `diagnostics`：
    - 作用：集中收集 todo/error/warn。
    - 建立：指向 `ConvertDriver::diagnostics_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `messages_`: `std::vector<ConvertDiagnostic>` -> 诊断条目列表。
      - `hasError_`: `bool` -> 是否已记录 error。
      - `onError_`: `std::function<void()>` -> error 回调（可抛出 `ConvertAbort`）。
    - 输出：供 CLI 输出与测试断言使用。
  - `logger`：
    - 作用：统一日志出口，支持等级与 tag 过滤。
    - 建立：指向 `ConvertDriver::logger_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `enabled_`: `bool` -> 是否启用日志。
      - `level_`: `ConvertLogLevel` -> 当前日志等级阈值。
      - `tags_`: `std::unordered_set<std::string>` -> 允许的 tag 集合。
      - `sink_`: `Sink`（`std::function<void(const ConvertLogEvent&)>`）-> 日志输出回调。
    - 输出：供调试/诊断流输出。
  - `planCache`：
    - 作用：模块计划与中间产物缓存，负责去重与状态管理。
    - 建立：指向 `ConvertDriver::planCache_`。
    - 子成员（内部状态 -> 类型 -> 含义）：
      - `entries_`: `std::unordered_map<PlanKey, PlanEntry>` -> 计划状态与产物集合。
      - `mutex_`: `std::mutex` -> 并行访问保护。
    - 输出：供 Pass1~Pass7 写入与读取 `ModulePlan`/`PlanArtifacts`。
  - `planQueue`：
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
- 总览：
  - 定位：模块计划的“状态 + 产物”核心单元，承载一个模块从计划到写回的生命周期。
  - 角色：PlanCache 的 value，控制并发去重与产物落地。
- 字段总览（字段 -> 类型 -> 含义）：
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
  - `PlanKey`：唯一标识参数特化模块（`body + paramSignature`）。
  - `PlanCache`：`PlanKey -> PlanEntry` 的并发缓存与去重容器。
  - `PlanArtifacts`：`PlanEntry` 内部的中间产物容器。

### ModulePlan
- 总览：
  - 定位：模块级静态计划，集中记录端口/信号/实例/读写关系等“长期稳定事实”。
  - 角色：Pass1~Pass7 的共享输入与写入目标。
- 字段总览（字段 -> 类型 -> 含义）：
  - `body`: `const slang::ast::InstanceBodySymbol*` -> 模块实例体指针。
  - `symbolTable`: `PlanSymbolTable` -> 模块内符号驻留表。
  - `moduleSymbol`: `PlanSymbolId` -> 模块名（或回退名）的 intern 结果。
  - `ports`: `std::vector<PortInfo>` -> 端口骨架记录。
  - `signals`: `std::vector<SignalInfo>` -> 信号骨架记录。
  - `rwOps`: `std::vector<RWOp>` -> 读写访问记录（详见 RWOp）。
  - `memPorts`: `std::vector<MemoryPortInfo>` -> memory 端口记录（详见 MemoryPortInfo）。
  - `instances`: `std::vector<InstanceInfo>` -> 子实例记录（详见 InstanceInfo）。
- 字段详解：
  - `body`：
    - 作用：提供 AST 成员遍历入口。
    - 建立：`ModulePlanner::plan` 绑定。
    - 输出：供 Pass1~Pass3 遍历 AST。
  - `symbolTable`：
    - 作用：统一管理模块内符号名与 `PlanSymbolId`。
    - 建立：Pass1 在收集端口/信号/实例时调用 `intern`。
    - 输出：供后续 pass 查找/反查符号名（详见 PlanSymbolTable）。
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
- 设计意图：
  - `ModulePlan` 放在顶层是为了保存“长期稳定事实”（符号、宽度、读写关系等），
    后续 Pass 都需要它作为共同基准。
  - `LoweringPlan/WriteBackPlan` 被包在 `PlanArtifacts` 中是为了标识“阶段性产物”，
    可按需生成、缓存或释放，避免与稳定骨架耦合。
  - 这种分层明确生命周期边界，便于并行/增量执行时隔离写入冲突与内存管理。

### PlanSymbolTable
- 总览：
  - 定位：模块内符号驻留与索引化容器，提供 `string_view -> PlanSymbolId` 的稳定映射。
  - 目的：避免重复分配字符串，保证 name 查找与反查的性能与一致性。
- 字段总览（字段 -> 类型 -> 含义）：
  - `storage_`: `std::deque<std::string>` -> 持有符号文本的稳定存储。
  - `index_`: `std::unordered_map<std::string_view, PlanSymbolId>` -> 名称到 `PlanSymbolId` 的映射。
- 字段详解：
  - `storage_`：
    - 作用：持有字符串并保证 `string_view` 稳定指向。
    - 建立：`intern` 时追加。
    - 输出：供 `text(id)` 返回稳定视图。
  - `index_`：
    - 作用：快速查找既有符号并去重。
    - 建立：`intern` 写入，`lookup` 仅查询。
    - 输出：支撑 `lookup` 与去重逻辑。
- 接口总览（方法 -> 语义）：
  - `intern(text)`: 插入或复用已有符号，返回 `PlanSymbolId`。
  - `lookup(text)`: 查找符号，未找到返回无效 `PlanSymbolId`。
  - `text(id)`: 由 `PlanSymbolId` 反查符号文本。
  - `size()`: 当前驻留的符号数量。
- 使用约定：
  - 生成：Pass1 在收集端口/信号/实例名时调用 `intern`。
  - 访问：Pass2/Pass3 通过 `lookup` 将 AST 名称映射到索引。
  - 输出：诊断与日志通过 `text(id)` 获取稳定名称。
- 索引化主键：
  - `PlanSymbolId` 作为主键；`PortId/SignalId` 等均是 `std::vector` 索引。
  - 访问模式只追加写入，避免中途删除带来的引用失效。

### AccessSite
- 总览：
  - 定位：单次访问点的最小描述，用于保留访问次数与顺序信息。
  - 角色：为端口数量推断与写入优先级分析提供原始依据。
  - 说明：只保存源码位置与序号，不直接保存 slang AST 节点指针。
- 字段总览（字段 -> 类型 -> 含义）：
  - `location`: `slang::SourceLocation` -> 访问点源码位置（表达式起点）。
  - `sequence`: `uint32_t` -> RWAnalyzer 访问序号（遍历顺序）。
- 字段详解：
  - `location`：
    - 作用：标记访问来源，便于调试与后续端口分配。
    - 建立：RWAnalyzer 取 `expr.sourceRange.start()`。
    - 输出：用于日志/诊断或端口分配策略；如需定位 AST，需额外索引或通过 SourceManager 反查。
  - `sequence`：
    - 作用：保留遍历顺序，支持写入优先级分析。
    - 建立：RWAnalyzer 内部递增计数生成。
    - 输出：供后续阶段排序/优先级判断。

### RWOp
- 总览：
  - 定位：`ModulePlan.rwOps` 的单条读写记录，描述“哪个信号在什么控制域发生读/写”。
  - 角色：下游做读写归因、写回策略与端口数量分析的输入。
- 字段总览（字段 -> 类型 -> 含义）：
  - `target`: `SignalId` -> 目标信号索引（指向 `ModulePlan.signals`）。
  - `domain`: `ControlDomain` -> 控制域语义（comb/seq/latch/unknown）。
  - `isWrite`: `bool` -> 是否为写访问（false 表示读）。
  - `sites`: `std::vector<AccessSite>` -> 访问点列表（用于端口数量/写入优先级分析）。
- 字段详解：
  - `target`：
    - 作用：绑定具体信号条目，避免重复字符串解析。
    - 建立：RWAnalyzer 通过 `PlanSymbolId -> SignalId` 映射解析。
    - 输出：供 Pass4~Pass7 做读写归因与冲突检测。
  - `domain`：
    - 作用：标注读写发生的控制域，用于区分 combinational 与 sequential 语义。
    - 建立：RWAnalyzer 依据 `ProceduralBlockSymbol` 与 timing control 分类。
    - 输出：影响后续写回策略、memory 端口同步推断。
  - `isWrite`：
    - 作用：区分读/写访问；与 `target/domain` 共同构成归并键。
    - 建立：RWAnalyzer 在遍历 LHS/RHS 时决定。
    - 输出：指导后续 Pass 在生成 Value/Op 时使用正确的数据流方向。
  - `sites`：
    - 作用：保留所有访问点，避免归并后丢失访问次数与位置。
    - 建立：RWAnalyzer 在每次访问时追加 `AccessSite{location, sequence}`。
    - 输出：用于端口数量推断与寄存器写入优先级分析。
- 生成与使用约定：
  - 生成：Pass3 负责填充并按 `(target, domain, isWrite)` 归并；访问点以 `sites` 保留。
  - 使用：Pass4~Pass7 以 `rwOps` 作为读写关系输入，不直接回扫 AST。

### MemoryPortInfo
- 总览：
  - 定位：`ModulePlan.memPorts` 的单条 memory 端口记录，描述“某个 memory 在某类访问形态下需要的端口能力”。
  - 角色：Pass6 细化 memory 端口与数量规划的输入。
- 字段总览（字段 -> 类型 -> 含义）：
  - `memory`: `SignalId` -> memory 信号索引（指向 `ModulePlan.signals`）。
  - `isRead`: `bool` -> 是否存在读访问。
  - `isWrite`: `bool` -> 是否存在写访问。
  - `isMasked`: `bool` -> 是否带字节/位写掩码（当前版本恒为 false，预留）。
  - `isSync`: `bool` -> 是否同步端口（顺序域访问为 true）。
  - `hasReset`: `bool` -> 是否带显式 reset 语义（当前版本恒为 false，预留）。
  - `sites`: `std::vector<AccessSite>` -> 访问点列表（用于端口数量推断）。
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
  - `sites`：
    - 作用：保留每次访问的来源位置，用于推断多读口/多写口数量。
    - 建立：RWAnalyzer 在每次 memory 访问时追加 `AccessSite`。
    - 输出：供 Pass6 或后续阶段决定端口个数与分配。
- 生成与归并约定：
  - 生成：Pass3 在识别到 `SignalInfo.memoryRows > 0` 的读/写访问时创建。
  - 归并：以 `memory + isRead + isWrite + isMasked + isSync + hasReset` 为 key 归并，
    访问点通过 `sites` 保留。
  - 使用：Pass6 以 `memPorts` 为输入，不回扫 AST；端口数量与拆分策略在 Pass6 决定。

### InstanceInfo
- 总览：
  - 定位：`ModulePlan.instances` 的单条子实例记录，描述“当前模块里包含哪些实例、它们指向哪个 module 体、是否黑盒”。
  - 角色：实例层次保留、任务发现与黑盒记录。
- 字段总览（字段 -> 类型 -> 含义）：
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
    - 输出：用于生成子模块的 PlanKey 与后续实例连接的目标模块名。
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
  - 任务发现：每条 `InstanceInfo` 关联一个子模块 PlanKey，由 Pass1 递归投递到队列。
  - 使用：Pass4~Pass7 消费 `instances` 构建实例层级与连接关系，不回扫 AST。

### LoweringPlan
- 总览：
  - 定位：`LoweringPlan` 记录 Pass4~Pass6 的表达式降级结果。
  - 角色：`LoweringPlan` 为 StmtLowerer/MemoryPortLowerer 的共享输入。
- LoweringPlan 字段总览（字段 -> 类型 -> 含义）：
  - `values`: `std::vector<ExprNode>` -> 降级后的表达式节点。
  - `roots`: `std::vector<LoweredRoot>` -> 每个 RHS root 的入口。
  - `tempSymbols`: `std::vector<PlanSymbolId>` -> 操作节点分配的临时名。
- 类型别名说明：
  - `ExprNodeId`：等同于 `PlanIndex`，用于索引 `LoweringPlan.values`。
- LoweringPlan 字段详解：
  - `values`：
    - 作用：保存降级后的表达式节点，节点之间通过索引引用。
    - 建立：Pass4 对 RHS 表达式树递归降级时追加。
    - 输出：供 Pass5/Pass6 解析依赖关系与写回意图。
  - `roots`：
    - 作用：标记每条 RHS 表达式的入口节点索引。
    - 建立：Pass4 处理赋值语句时收集 RHS 入口。
    - 输出：供后续 pass 以 root 为起点回溯依赖。
  - `tempSymbols`：
    - 作用：为操作节点分配稳定的临时名。
    - 建立：Pass4 每创建一个操作节点就追加一个 temp symbol。
    - 输出：供写回/生成阶段绑定临时值命名。
- ExprNode 字段总览（字段 -> 类型 -> 含义）：
  - `kind`: `ExprNodeKind` -> Constant/Symbol/Operation。
  - `op`: `grh::ir::OperationKind` -> 操作节点类型（仅 Operation 有效）。
  - `symbol`: `PlanSymbolId` -> 命名值节点绑定的符号名。
  - `tempSymbol`: `PlanSymbolId` -> 操作节点的临时符号名。
  - `literal`: `std::string` -> 常量文本。
  - `operands`: `std::vector<ExprNodeId>` -> 操作数索引。
  - `location`: `slang::SourceLocation` -> 源码位置。
- ExprNode 字段详解：
  - `kind`：
    - 作用：决定节点类型以及哪些字段有效。
    - 输出：Pass5/Pass6 依据 kind 解析节点语义。
  - `op`：
    - 作用：记录操作节点的 GRH 操作类型。
    - 约束：仅 `kind == Operation` 时有效。
  - `symbol`：
    - 作用：记录命名值对应的 PlanSymbolId。
    - 约束：仅 `kind == Symbol` 时有效。
  - `tempSymbol`：
    - 作用：记录操作节点的临时符号名。
    - 约束：仅 `kind == Operation` 时有效。
  - `literal`：
    - 作用：记录常量的文本表示。
    - 约束：仅 `kind == Constant` 时有效。
  - `operands`：
    - 作用：记录操作节点的输入索引。
    - 约束：仅 `kind == Operation` 时有效。
  - `location`：
    - 作用：保留源码位置信息，便于诊断与日志。
- LoweredRoot 字段总览（字段 -> 类型 -> 含义）：
  - `value`: `ExprNodeId` -> 根节点索引。
  - `location`: `slang::SourceLocation` -> 源码位置。
- LoweredRoot 字段详解：
  - `value`：
    - 作用：指向 `values` 中的根节点。
    - 输出：作为每条 RHS 的入口。
  - `location`：
    - 作用：记录 root 的源码位置。
- 举例说明：
  - 表达式：`assign y = (a & b) ? ~c : (a | b);`
  - 操作节点：`kAnd(a,b)`、`kNot(c)`、`kOr(a,b)`、`kMux(cond, lhs, rhs)`。
  - `LoweredRoot.value`：指向 `kMux` 节点索引（RHS 顶层根）。
  - `values`（示意顺序）：
    - `Symbol(a)`, `Symbol(b)`, `Op(kAnd, [a,b])`,
      `Symbol(c)`, `Op(kNot, [c])`, `Symbol(a)`, `Symbol(b)`,
      `Op(kOr, [a,b])`, `Op(kMux, [and, not, or])`
  - `roots`：`[kMux 节点索引]`
  - `tempSymbols`：`[_expr_tmp_0, _expr_tmp_1, _expr_tmp_2, _expr_tmp_3]`（对应 and/not/or/mux）
  - 说明：`a/b` 在 AST 中出现两次时会生成两个 `Symbol` 节点，因此 `values` 共 9 项。
- 生成与使用约定：
  - 生成：Pass4 扫描过程块与连续赋值，降级 RHS 并写入 `PlanArtifacts.loweringPlan`。
  - 使用：Pass5~Pass6 读取 `loweringPlan`，不回扫 AST。

### WriteBackPlan
- 总览：
  - 暂未展开，后续补充写回合并与 guard/mux 决策细节。

### Diagnostics / Logger
- 总览：
  - `ConvertDiagnostics`：统一收集 todo/error/warn，保留源码位置信息。
  - `ConvertLogger`：level/tag 过滤的可控日志接口。

## Pass 中间数据结构
- 总览：
  - 定位：单个 Pass 内部的临时结构/索引/去重表，不进入 `ModulePlan` 或 `PlanArtifacts`。
  - 目的：降低跨 Pass 的耦合与状态污染，便于并行与复用。
- 归属（Pass -> 中间结构）：
  - Pass1：ParameterSnapshot
  - Pass2：TypeResolution / PortIndexMap / SignalIndexMap
  - Pass3：RWAnalyzerState / RWVisitor

### Pass1: SymbolCollector
- 总览：
  - 作用：在符号收集阶段提取参数绑定快照，支撑参数特化与黑盒参数记录。
  - 输出：PlanKey.paramSignature 与 InstanceInfo.parameters。
- 子结构：
  - `ParameterSnapshot`：
    - 作用：抽取参数绑定的序列化结果，区分实例特化并保存黑盒参数。
    - 字段总览（字段 -> 类型 -> 含义）：
      - `signature`: `std::string` -> `name=value;...` 的序列化结果。
      - `parameters`: `std::vector<InstanceParameter>` -> 黑盒实例的参数条目。
    - 字段详解：
      - `signature`：用于 PlanKey.paramSignature，保证不同参数取值的实例不会合并。
      - `parameters`：仅对黑盒实例保留，供 GRH 输出参数名和值。
    - 建立：遍历 `InstanceBodySymbol.getParameters()`（跳过 `localparam`），
      读取 `ParameterSymbol` 或 `TypeParameterSymbol` 并序列化。
    - 输出：
      - `signature` -> PlanKey.paramSignature。
      - `parameters` -> InstanceInfo.parameters（仅黑盒实例保留）。

### Pass2: TypeResolver
- 总览：
  - 作用：用临时分析结果与索引表支撑类型解析，避免对 ModulePlan 进行半成品写入。
  - 输出：回写 PortInfo/SignalInfo 的宽度、符号与维度信息。
- 子结构：
  - `TypeResolution`：
    - 作用：承载单个端口/信号的类型分析结果。
    - 字段总览（字段 -> 类型 -> 含义）：
      - `width`: `int32_t` -> 位宽。
      - `isSigned`: `bool` -> 符号性。
      - `memoryRows`: `int64_t` -> fixed unpacked 维度乘积。
      - `packedDims`: `std::vector<int32_t>` -> packed 维度（外到内）。
      - `unpackedDims`: `std::vector<int32_t>` -> unpacked 维度（外到内）。
    - 字段详解：
      - `width/isSigned`：写回端口与信号的最终基本类型属性。
      - `packedDims/unpackedDims/memoryRows`：支撑 memory 判定与维度保留。
    - 建立：`analyzePortType`/`analyzeSignalType` 解析 `slang::ast::Type` 时生成。
    - 输出：写回 PortInfo/SignalInfo。
  - `PortIndexMap`（`std::vector<PortId>`）：
    - 作用：`PlanSymbolId -> PortId` 映射，避免 O(n^2) 查找。
    - 说明：`PortId` 即 `ModulePlan.ports` 的下标索引。
    - 建立：遍历 `ModulePlan.ports` 构建。
    - 输出：仅在 Pass2 内部使用。
  - `SignalIndexMap`（`std::vector<SignalId>`）：
    - 作用：`PlanSymbolId -> SignalId` 映射。
    - 说明：`SignalId` 即 `ModulePlan.signals` 的下标索引。
    - 建立：遍历 `ModulePlan.signals` 构建。
    - 输出：仅在 Pass2 内部使用。

### Pass3: RWAnalyzer
- 总览：
  - 作用：在读写分析阶段集中维护索引、归并与 AST 访问入口。
  - 输出：`ModulePlan.rwOps` 与 `ModulePlan.memPorts`（并保留访问点）。
- 生命周期：
  - 实例粒度：每个模块仅创建一个 `RWAnalyzerState`，贯穿该模块的 Pass3 扫描。
  - 存储位置：仅在 Pass3 内部作为局部对象存在，不写入 `ModulePlan`；
    访问结果写入 `ModulePlan.rwOps`/`ModulePlan.memPorts`。
- 子结构：
  - `RWAnalyzerState`：
    - 数据类型：`struct`（定义于 `src/convert.cpp`，仅 Pass3 内部可见）。
    - 作用：保存索引表与去重索引，统一管理读写记录生成。
    - 字段总览（字段 -> 类型 -> 含义）：
      - `signalBySymbol`: `std::vector<SignalId>` -> `PlanSymbolId -> SignalId` 映射。
      - `rwKeys`: `std::unordered_map<uint64_t, RWOpId>` -> 读写记录 key 到 `rwOps` 索引。
      - `memKeys`: `std::unordered_map<uint64_t, MemoryPortId>` -> memory 端口 key 到 `memPorts` 索引。
      - `nextSite`: `uint32_t` -> 访问点递增序号。
    - 字段详解：
      - `signalBySymbol`：定位目标信号的快速映射表。
      - `rwKeys`：
        - key 语义：`(SignalId, ControlDomain, isWrite)`。
        - 编码：`encodeRWKey` 将 `id<<3 | domain<<1 | isWrite` 写入 `uint64_t`。
      - `memKeys`：
        - key 语义：`(SignalId, isRead, isWrite, isMasked, isSync, hasReset)`。
        - 编码：`encodeMemKey` 将 `id<<5` 与 5 个标志位打包进 `uint64_t`。
      - `nextSite`：为每次访问分配顺序号，便于后续优先级分析。
    - 建立：扫描 `ModulePlan.signals` 构建 `signalBySymbol` 并初始化去重索引。
    - 输出：通过 `recordRead/recordWrite` 写入 `rwOps`/`memPorts`，并追加 `AccessSite`。
  - `RWVisitor`：
    - 数据类型：`class`（继承 `slang::ast::ASTVisitor`，定义于 `src/convert.cpp`）。
    - 作用：区分 LHS/RHS，并将 NamedValue 记为读/写访问。
    - 建立：每个过程块/连续赋值构造一个 visitor，绑定当前 `ControlDomain`。
    - 输出：调用 `RWAnalyzerState` 写入访问记录。

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
