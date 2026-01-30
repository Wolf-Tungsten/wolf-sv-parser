# Convert 架构（静态视角）

> 本文聚焦数据结构与并行策略，描述 Convert 的静态组织方式。

## 目录
- 1. 目标与边界
- 2. 设计原则
- 3. 核心数据结构
  - 3.1. ConvertContext
  - 3.2. PlanKey
  - 3.3. PlanEntry（核心结构）
  - 3.4. ModulePlan
  - 3.5. PlanSymbolTable
  - 3.6. AccessSite
  - 3.7. RWOp
  - 3.8. MemoryPortInfo
  - 3.9. InstanceInfo
  - 3.10. LoweringPlan
    - 3.10.1. 总览与角色
    - 3.10.2. 字段总览
    - 3.10.3. 类型别名
    - 3.10.4. 字段详解
    - 3.10.5. ExprNode
    - 3.10.6. LoweredRoot
    - 3.10.7. WriteIntent / WriteSlice
    - 3.10.8. LoweredStmt
    - 3.10.9. 举例说明
    - 3.10.10. MemoryReadPort / MemoryWritePort
  - 3.11. WriteBackPlan
  - 3.12. Diagnostics / Logger
- 4. Pass 中间数据结构
  - 4.1. Pass1: SymbolCollector
  - 4.2. Pass2: TypeResolver
  - 4.3. Pass3: RWAnalyzer
  - 4.4. Pass5: StmtLowerer 内部临时结构
    - 4.4.1. 动态 break/continue 完整示例（分 + 案例）
- 5. Pass 与数据结构关系图
- 6. 并行化策略（以模块为粒度）
  - 6.1. 并行边界
  - 6.2. 任务与调度
  - 6.3. 去重与同步
  - 6.4. 数据一致性
  - 6.5. 诊断与日志
- 7. GRH 输出契约
- 8. 代码骨架落位

## 1. 目标与边界
- 输入：slang AST（`slang::ast::*`），由前端构建的 `Compilation` 与 `RootSymbol` 提供。
- 输出：GRH IR（`grh::ir::Netlist` / `Graph` / `Value` / `Operation`）。
- 职责：把已解析并类型化的 AST 转为结构化网表，并输出可定位的诊断信息。
- 非职责：覆盖所有 SV 特性、完整时序/综合语义证明。

## 2. 设计原则
- 分阶段：先收集与分析，再生成 IR，避免边扫边写导致状态纠缠。
- 以 slang 类型系统为源：宽度、符号、packed/unpacked 维度、参数值均从 `slang::ast::Type` 与常量求值获取。
- 显式的数据模型：使用 Plan/Info 结构保存信号、端口、读写关系与控制域，降低隐式 memo。
- SSA 与四态：GRH 以 SSA 表达，四态逻辑语义保持从 slang 到 GRH 一致。
- 模块粒度隔离：每个 `InstanceBodySymbol` 形成独立的 ModulePlan 与 Graph，顶层与子实例一致处理。
- 层次保留：不做全局扁平化，实例关系通过 kInstance 维护。
- 可控日志：提供统一的调试日志接口，默认静默，可按级别与模块启用。

## 3. 核心数据结构
- 总览：
  - 定位：描述 Convert 的核心模型与共享状态，定义模块计划、符号索引与访问关系的稳定形态。
  - 结构清单：PlanKey、ConvertContext、PlanEntry、ModulePlan、PlanSymbolTable、AccessSite、
    RWOp、MemoryPortInfo、InstanceInfo、LoweringPlan/WriteBackPlan、Diagnostics/Logger。

### 3.1. ConvertContext
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
      - `maxLoopIterations`: `uint32_t` -> Pass5 静态展开循环的最大迭代上限。
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

### 3.2. PlanKey
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

### 3.3. PlanEntry（核心结构）
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
    - 建立：在 `PlanEntry` 构造时默认存在；内容由 Pass4/Pass6 写入。
    - 子成员（字段 -> 类型 -> 含义）：
      - `loweringPlan`: `std::optional<LoweringPlan>` -> Pass4/Pass5/Pass6/Pass7 产物。
      - `writeBackPlan`: `std::optional<WriteBackPlan>` -> Pass6 产物。
    - 输出：供 Pass8 使用。
- 关联结构（围绕 PlanEntry 的容器/索引）：
  - `PlanKey`：唯一标识参数特化模块（`body + paramSignature`）。
  - `PlanCache`：`PlanKey -> PlanEntry` 的并发缓存与去重容器。
  - `PlanArtifacts`：`PlanEntry` 内部的中间产物容器。

### 3.4. ModulePlan
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
      - `unpackedDims`: `std::vector<UnpackedDimInfo>` -> unpacked 维度（外到内，含 left/right/extent）。
- 设计意图：
  - `ModulePlan` 放在顶层是为了保存“长期稳定事实”（符号、宽度、读写关系等），
    后续 Pass 都需要它作为共同基准。
  - `LoweringPlan/WriteBackPlan` 被包在 `PlanArtifacts` 中是为了标识“阶段性产物”，
    可按需生成、缓存或释放，避免与稳定骨架耦合。
  - 这种分层明确生命周期边界，便于并行/增量执行时隔离写入冲突与内存管理。

### 3.5. PlanSymbolTable
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

### 3.6. AccessSite
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

### 3.7. RWOp
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

### 3.8. MemoryPortInfo
- 总览：
  - 定位：`ModulePlan.memPorts` 的单条 memory 端口记录，描述“某个 memory 在某类访问形态下需要的端口能力”。
  - 角色：Pass7 细化 memory 端口与数量规划的输入。
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
    - 输出：供 Pass7 选择 memory lowering 策略与端口模板。
  - `isRead`/`isWrite`：
    - 作用：描述端口读写能力；`true/false` 组合表达只读/只写/读写口。
    - 建立：RWAnalyzer 在读/写访问被识别时记录。
    - 输出：影响 Pass7 生成的端口类型与连接逻辑。
  - `isMasked`：
    - 作用：标识写端口是否需要掩码能力（如 byte-enable）。
    - 建立：当前实现未推断掩码访问，固定为 false。
    - 输出：保留给后续支持掩码写的 lowering。
  - `isSync`：
    - 作用：标识端口是否为同步访问口。
    - 建立：RWAnalyzer 依据访问控制域推断（顺序域 = true）。
    - 输出：影响 Pass7 选择 sync/async memory 端口形态。
  - `hasReset`：
    - 作用：标识 memory 访问是否依赖显式 reset。
    - 建立：当前实现未推断 reset 语义，固定为 false。
    - 输出：预留给后续带 reset 端口的 lowering。
  - `sites`：
    - 作用：保留每次访问的来源位置，用于推断多读口/多写口数量。
    - 建立：RWAnalyzer 在每次 memory 访问时追加 `AccessSite`。
    - 输出：供 Pass7 或后续阶段决定端口个数与分配。
- 生成与归并约定：
  - 生成：Pass3 在识别到 `SignalInfo.memoryRows > 0` 的读/写访问时创建。
  - 归并：以 `memory + isRead + isWrite + isMasked + isSync + hasReset` 为 key 归并，
    访问点通过 `sites` 保留。
  - 使用：Pass7 以 `memPorts` 为输入，不回扫 AST；端口数量与拆分策略在 Pass7 决定。

### 3.9. InstanceInfo
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

### 3.10. LoweringPlan
#### 3.10.1. 总览与角色
- 定位：`LoweringPlan` 记录 Pass4~Pass7 的表达式降级结果。
- 角色：`LoweringPlan` 为 StmtLowerer/WriteBack/MemoryPortLowerer 的共享输入。

#### 3.10.2. 字段总览
- `values`: `std::vector<ExprNode>` -> 降级后的表达式节点。
- `roots`: `std::vector<LoweredRoot>` -> 每个 RHS root 的入口。
- `tempSymbols`: `std::vector<PlanSymbolId>` -> 操作节点分配的临时名。
- `writes`: `std::vector<WriteIntent>` -> 赋值写回意图与 guard 信息。
- `loweredStmts`: `std::vector<LoweredStmt>` -> 按语句顺序保存 Write/Display/Assert/DpiCall。
- `memoryReads`: `std::vector<MemoryReadPort>` -> 读端口降级条目。
- `memoryWrites`: `std::vector<MemoryWritePort>` -> 写端口降级条目（含 mask）。

#### 3.10.3. 类型别名
- `ExprNodeId`：等同于 `PlanIndex`，用于索引 `LoweringPlan.values`。

#### 3.10.4. 字段详解
- `values`：
  - 作用：保存降级后的表达式节点，节点之间通过索引引用。
  - 建立：Pass4 对 RHS 表达式树递归降级时追加。
  - 输出：供 Pass5~Pass7 解析依赖关系与写回意图。
  - `roots`：
    - 作用：标记每条 RHS 表达式的入口节点索引。
    - 建立：Pass4 处理赋值语句时收集 RHS 入口。
    - 输出：供后续 pass 以 root 为起点回溯依赖。
  - `tempSymbols`：
    - 作用：为操作节点分配稳定的临时名。
    - 建立：Pass4 每创建一个操作节点就追加一个 temp symbol。
    - 输出：供写回/生成阶段绑定临时值命名。
  - `writes`：
    - 作用：记录每条赋值语句的写回意图、guard 与控制域，并携带 LHS 切片信息。
    - 建立：Pass5 解析语句与控制流后追加。
    - 输出：供 Pass6 合并写回与 guard/mux 决策；静态展开的循环会追加多条写回意图。
    - 说明：guard 本身以 `ExprNode` 形式保存在 `values`，`WriteIntent.guard` 仅保存对应节点索引；
      动态 break/continue 会额外生成 `loopAlive/flowGuard` 相关节点并参与 guard 组合，但不引入新结构字段。
    - loopAlive/flowGuard 存储方式：
      - `loopAlive` 与 `flowGuard` 都是普通 guard 表达式，落在 `LoweringPlan.values` 中；
        其节点索引通过 `WriteIntent.guard` 间接引用（例如 `guard = flowGuard && branchGuard`）。
      - Pass5 内部使用栈来维护 `loopAlive/flowGuard`，属于 Pass 内部临时状态，
        不会作为独立字段持久化到 `LoweringPlan`。
  - `loweredStmts`：
    - 作用：按语句顺序记录 Write/Display/Assert/DpiCall，保留副作用语句的相对顺序。
    - 建立：Pass5 在解析语句时追加；WriteIntent 会同时进入 `writes` 与 `loweredStmts`。
    - 输出：供 Pass6 写回合并与调试/DPI 转换使用；`loweredStmts` 是语句级顺序的唯一入口。
  - `memoryReads`：
    - 作用：记录 memory 读端口的降级结果（地址/事件绑定）。
    - 建立：Pass7 从 `loweredStmts` 的表达式树中识别 `mem[addr]` 访问并追加。
    - 输出：供后续 GraphAssembly 生成 `kMemoryReadPort`/同步读寄存器。
  - `memoryWrites`：
    - 作用：记录 memory 写端口降级结果（`updateCond/data/mask/event`）。
    - 建立：Pass7 从写回语句中识别 memory 写入并追加。
    - 输出：供后续 GraphAssembly 生成 `kMemoryWritePort`/`kMemoryMaskWritePort`。

#### 3.10.5. ExprNode
- 字段总览（字段 -> 类型 -> 含义）：
  - `kind`: `ExprNodeKind` -> Constant/Symbol/Operation。
  - `op`: `grh::ir::OperationKind` -> 操作节点类型（仅 Operation 有效）。
  - `symbol`: `PlanSymbolId` -> 命名值节点绑定的符号名。
  - `tempSymbol`: `PlanSymbolId` -> 操作节点的临时符号名。
  - `literal`: `std::string` -> 常量文本。
  - `operands`: `std::vector<ExprNodeId>` -> 操作数索引。
  - `location`: `slang::SourceLocation` -> 源码位置。
- 字段详解：
  - `kind`：
    - 作用：决定节点类型以及哪些字段有效。
    - 输出：Pass5~Pass7 依据 kind 解析节点语义。
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

#### 3.10.6. LoweredRoot
- 字段总览（字段 -> 类型 -> 含义）：
  - `value`: `ExprNodeId` -> 根节点索引。
  - `location`: `slang::SourceLocation` -> 源码位置。
- 字段详解：
  - `value`：
    - 作用：指向 `values` 中的根节点。
    - 输出：作为每条 RHS 的入口。
  - `location`：
    - 作用：记录 root 的源码位置。

#### 3.10.7. WriteIntent / WriteSlice
- WriteIntent 字段总览（字段 -> 类型 -> 含义）：
  - `target`: `PlanSymbolId` -> 写回目标符号。
  - `slices`: `std::vector<WriteSlice>` -> LHS 位选/范围选链路；为空表示整信号写回。
  - `value`: `ExprNodeId` -> RHS root 节点索引。
  - `guard`: `ExprNodeId` -> guard 条件节点索引（无 guard 时为 invalid）。
  - `domain`: `ControlDomain` -> 控制域（comb/seq/latch/unknown）。
  - `isNonBlocking`: `bool` -> 是否非阻塞赋值。
  - `location`: `slang::SourceLocation` -> 写回语句位置。
- WriteSlice 字段总览（字段 -> 类型 -> 含义）：
  - `kind`: `WriteSliceKind` -> BitSelect/RangeSelect/MemberSelect。
  - `rangeKind`: `WriteRangeKind` -> Simple/IndexedUp/IndexedDown（仅 RangeSelect 有效）。
  - `index`: `ExprNodeId` -> bit-select 索引（仅 BitSelect 有效）。
  - `left`: `ExprNodeId` -> range 左表达式（RangeSelect）。
  - `right`: `ExprNodeId` -> range 右表达式（RangeSelect）。
  - `member`: `PlanSymbolId` -> 成员名（MemberSelect）。
  - `location`: `slang::SourceLocation` -> 片段选择位置。

#### 3.10.8. LoweredStmt
- LoweredStmt 字段总览（字段 -> 类型 -> 含义）：
  - `kind`: `LoweredStmtKind` -> Write/Display/Assert/DpiCall。
  - `op`: `grh::ir::OperationKind` -> 对应的 GRH 操作类型（Display/Assert/DpiCall）。
  - `updateCond`: `ExprNodeId` -> 语句触发条件（无条件时为常量 `1'b1`）。
  - `eventEdges`: `std::vector<EventEdge>` -> 触发事件边沿列表（posedge/negedge）。
  - `eventOperands`: `std::vector<ExprNodeId>` -> 触发事件信号表达式列表。
  - `location`: `slang::SourceLocation` -> 语句位置。
  - `write`: `WriteIntent` -> Write 语句内容（仅 `kind == Write` 有效）。
  - `display`: `DisplayStmt` -> 调试输出参数（仅 `kind == Display` 有效）。
  - `assertion`: `AssertStmt` -> 断言信息（仅 `kind == Assert` 有效）。
  - `dpiCall`: `DpiCallStmt` -> DPI 调用信息（仅 `kind == DpiCall` 有效）。
- DisplayStmt 字段总览：
  - `formatString`: `std::string` -> `$display/$write/$strobe` 的格式字符串。
  - `displayKind`: `std::string` -> display/write/strobe。
  - `args`: `std::vector<ExprNodeId>` -> 输出参数表达式。
- AssertStmt 字段总览：
  - `condition`: `ExprNodeId` -> 断言条件表达式。
  - `message`: `std::string` -> 断言信息文本。
  - `severity`: `std::string` -> fatal/error/warning/info。
- DpiCallStmt 字段总览：
  - `targetImportSymbol`: `std::string` -> 对应 `kDpicImport` 的 symbol 名称。
  - `inArgNames`: `std::vector<std::string>` -> 输入形参名（按 formal 顺序）。
  - `outArgNames`: `std::vector<std::string>` -> 输出形参名（按 formal 顺序）。
  - `inArgs`: `std::vector<ExprNodeId>` -> 输入实参表达式。
  - `results`: `std::vector<PlanSymbolId>` -> 输出实参（可选的 return 位于索引 0）。
  - `hasReturn`: `bool` -> 是否有返回值。
- 事件绑定约束：
  - 仅当过程块包含 edge-sensitive 事件列表时才会生成 Display/Assert/DpiCall；
    否则语句被丢弃并生成诊断。
  - `eventEdges.size()` 必须与 `eventOperands.size()` 一致。

#### 3.10.9. 举例说明
- 表达式降级（Pass4）：
    - 输入：`assign y = (a & b) ? ~c : (a | b);`
    - 操作节点：`kAnd(a,b)`、`kNot(c)`、`kOr(a,b)`、`kMux(cond, lhs, rhs)`。
    - `LoweredRoot.value`：指向 `kMux` 节点索引（RHS 顶层根）。
    - `values`（示意顺序）：
      - `0: Symbol(a)`
      - `1: Symbol(b)`
      - `2: Op(kAnd, [0,1])`
      - `3: Symbol(c)`
      - `4: Op(kNot, [3])`
      - `5: Symbol(a)`
      - `6: Symbol(b)`
      - `7: Op(kOr, [5,6])`
      - `8: Op(kMux, [2,4,7])`
    - `roots`：`[8]`
    - `tempSymbols`：`[2->tmp0, 4->tmp1, 7->tmp2, 8->tmp3]`
    - 说明：`a/b` 在 AST 中出现两次时会生成两个 `Symbol` 节点，因此 `values` 共 9 项。
- 分支语句（Pass5）：
    - if/else：
      - 输入：
        ```
        always_comb begin
          if (a) y = b; else y = c;
        end
        ```
      - 输出（示意）：
        - `values`（RHS + guard）：
          - `0: Symbol(b)`  (rhs0)
          - `1: Symbol(c)`  (rhs1)
          - `2: Symbol(a)`  (g0)
          - `3: Op(kLogicNot, [2])` (g1)
        - `roots`：`[0, 1]`
        - `writes`：
          - `WriteIntent{target=y, value=0, guard=2}`
      - `WriteIntent{target=y, value=1, guard=3}`

#### 3.10.10. MemoryReadPort / MemoryWritePort
- MemoryReadPort 字段总览（字段 -> 类型 -> 含义）：
  - `memory`: `PlanSymbolId` -> memory 符号名。
  - `signal`: `SignalId` -> memory 信号索引（未解析为 invalid）。
  - `address`: `ExprNodeId` -> 读地址表达式。
  - `data`: `ExprNodeId` -> 读数据表达式节点（`mem[addr]` 对应的 node）。
  - `isSync`: `bool` -> 是否顺序域读端口。
  - `updateCond`: `ExprNodeId` -> 顺序读的 enable 条件（无 guard 时为常量 1）。
  - `eventEdges`: `std::vector<EventEdge>` -> 顺序域事件边沿。
  - `eventOperands`: `std::vector<ExprNodeId>` -> 顺序域事件信号。
  - `location`: `slang::SourceLocation` -> 访问位置。
- MemoryWritePort 字段总览（字段 -> 类型 -> 含义）：
  - `memory`: `PlanSymbolId` -> memory 符号名。
  - `signal`: `SignalId` -> memory 信号索引（未解析为 invalid）。
  - `address`: `ExprNodeId` -> 写地址表达式。
  - `data`: `ExprNodeId` -> 写数据表达式（已按 bit/range 对齐）。
  - `mask`: `ExprNodeId` -> 写掩码表达式（全写时为全 1 常量）。
  - `updateCond`: `ExprNodeId` -> 写入条件（guard 合并，缺失时常量 1）。
  - `isMasked`: `bool` -> 是否为部分写入（mask != 全 1）。
  - `eventEdges`: `std::vector<EventEdge>` -> 顺序域事件边沿。
  - `eventOperands`: `std::vector<ExprNodeId>` -> 顺序域事件信号。
  - `location`: `slang::SourceLocation` -> 写入位置。
 - 处理约定（总览）：
  - 地址线性化：多维 unpacked memory 的地址使用
    `idx0 * stride0 + idx1 * stride1 + ...` 线性化。
    - 先做索引归一化：升序范围 `idx - left`，降序范围 `left - idx`。
  - 掩码写：`[base +: width]` / `[base -: width]` 生成动态 mask 与 data shift；
    简单范围在常量可求值时生成常量 mask。
    - Indexed part-select 的 width 需可静态求值（常量/模块参数/显式包参数/条件表达式/拼接复制表达式）；
      拼接/复制允许嵌套算术表达式，宽度以表达式宽度提示为准。
      越界时发出诊断并跳过，base 为动态表达式时给出 warning 并继续生成。
    - if/else if/else：
      - 输入：
        ```
        always_comb begin
          if (a) y = b;
          else if (c) y = d;
          else y = e;
        end
        ```
      - 输出（示意）：
        - `values`（RHS + guard）：
          - `0: Symbol(b)` (rhs0)
          - `1: Symbol(d)` (rhs1)
          - `2: Symbol(e)` (rhs2)
          - `3: Symbol(a)` (g0)
          - `4: Symbol(c)`
          - `5: Op(kLogicNot, [3])` (not a)
          - `6: Op(kLogicAnd, [5,4])` (g1)
          - `7: Op(kLogicNot, [4])` (not c)
          - `8: Op(kLogicAnd, [5,7])` (g2)
        - `roots`：`[0, 1, 2]`
        - `writes`：
          - `WriteIntent{y, value=0, guard=3}`
          - `WriteIntent{y, value=1, guard=6}`
          - `WriteIntent{y, value=2, guard=8}`
    - 说明：
      - 带 `matches` 的 pattern condition 当前不支持，Pass5 直接报错并跳过分支。
  - #### LHS 分片写回（Pass5）
    - 输入：
      ```
      always_comb begin
        y[3] = a;
        y[7:4] = b;
        y[idx +: 2] = c;
      end
      ```
    - 输出（示意）：
      - `values`（RHS + LHS 索引）：
        - `0: Symbol(a)` (rhs0)
        - `1: Symbol(b)` (rhs1)
        - `2: Symbol(c)` (rhs2)
        - `3: Const(3)` (bit index)
        - `4: Const(7)` (range left)
        - `5: Const(4)` (range right)
        - `6: Symbol(idx)` (range base)
        - `7: Const(2)` (range width)
      - `writes`：
        - `WriteIntent{target=y, slices=[{kind=BitSelect, index=3}], value=0, guard=invalid}`
        - `WriteIntent{target=y, slices=[{kind=RangeSelect, rangeKind=Simple, left=4, right=5}], value=1, guard=invalid}`
        - `WriteIntent{target=y, slices=[{kind=RangeSelect, rangeKind=IndexedUp, left=6, right=7}], value=2, guard=invalid}`
    - 说明：
      - `slices` 按 LHS 选择链路顺序记录；bit-select 仅填 `index`，range-select 填 `left/right`。
  - #### LHS 复合目标写回（Pass5）
    - 输入：
      ```
      always_comb begin
        {y, z} = data;
      end
      ```
    - 输出（示意）：
      - `values`（RHS + slice）：
        - `0: Symbol(data)` (rhs0)
        - `1: Const(7)` (slice high)
        - `2: Const(4)` (slice low)
        - `3: Op(kSliceDynamic, [0,1,2])` (rhs[7:4])
        - `4: Const(3)` (slice high)
        - `5: Const(0)` (slice low)
        - `6: Op(kSliceDynamic, [0,4,5])` (rhs[3:0])
      - `writes`：
        - `WriteIntent{target=y, value=3, guard=invalid}`
        - `WriteIntent{target=z, value=6, guard=invalid}`
    - 说明：
      - LHS concatenation/left-to-right streaming concat 会拆分成多条 WriteIntent。
    - 成员选择示例（含 memory 访问）：
      ```
      typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
      pair_t mem [0:3];
      always_ff @(posedge clk) begin
        mem[idx].hi <= a;
        mem[idx].lo <= b;
      end
      ```
    - 输出（示意）：
      - `writes`：
        - `WriteIntent{target=mem, slices=[{kind=BitSelect, index=expr(idx)}, {kind=MemberSelect, member=hi}], value=rhs(a), guard=invalid}`
        - `WriteIntent{target=mem, slices=[{kind=BitSelect, index=expr(idx)}, {kind=MemberSelect, member=lo}], value=rhs(b), guard=invalid}`
    - 说明：
      - `index=expr(idx)` 表示 index 字段保存 `idx` 的 ExprNodeId（由表达式降级得到）。
      - element select（数组索引）与 member select 都会追加到 slices 链路，顺序与 LHS 访问一致。
  - #### TimedStatement（Pass5）
    - 输入：
      ```
      always begin
        #1 y = a;
      end
      ```
    - 输出（示意）：
      - `writes`：`WriteIntent{target=y, value=rhs(a), guard=invalid}`
    - 说明：
      - timed control 仅触发 warning，降级时忽略 timing 语义并继续降低 body。
      - `wait` / `wait fork` / `wait order` / `event trigger` / `disable fork` 采取同样策略。
  - #### Case 语句（Pass5）
    - 输入：
      ```
      case(sel)
        2'b00,2'b01: y = a;
        2'b10:       y = b;
        default:     y = c;
      endcase
      ```
    - 输出（示意）：
      - `values`（RHS + match + guard）：
        - `0: Symbol(a)` (rhs0)
        - `1: Symbol(b)` (rhs1)
        - `2: Symbol(c)` (rhs2)
        - `3: Symbol(sel)`
        - `4: Const(2'b00)`
        - `5: Const(2'b01)`
        - `6: Const(2'b10)`
        - `7: Op(kCaseEq, [3,4])`
        - `8: Op(kCaseEq, [3,5])`
        - `9: Op(kLogicOr, [7,8])` (match0)
        - `10: Op(kCaseEq, [3,6])` (match1)
        - `11: Op(kLogicNot, [9])` (!match0)
        - `12: Op(kLogicAnd, [10,11])` (guard1)
        - `13: Op(kLogicOr, [9,10])` (anyMatch)
        - `14: Op(kLogicNot, [13])` (default)
      - `roots`：`[0, 1, 2]`
      - `writes`：
        - `WriteIntent{y, value=0, guard=9}`
        - `WriteIntent{y, value=1, guard=12}`
        - `WriteIntent{y, value=2, guard=14}`
    - 说明：
      - `casez/casex` 若 item 为常量，会构建 mask 并生成
        `(sel & mask) == (item & mask)`；若 item 非常量无法生成 mask，则回退到 `===`
        并发出 warning（可能不可综合）。
      - `case (...) matches`（PatternCase）当前不支持，Pass5 直接报错并跳过分支。
  - #### Case inside 语句（Pass5）
    - 规则：
      - item 为普通表达式：integral 使用 `kWildcardEq`（`==?`），其他类型使用 `kEq`。
      - item 为 `ValueRange`：
        - `[a:b]` -> `kGe(control, a)` 与 `kLe(control, b)` 再 `kLogicAnd`；
        - `[a +/- b]` -> `kSub(a, b)`/`kAdd(a, b)` + `kGe/kLe`；
        - `[a +%- b]` -> `kMul(a, b)`、`kDiv(..., 100)` 后再 `kSub/kAdd` + `kGe/kLe`。
      - guard 合并遵循 `base && match && !priorMatch`。
    - 示例（match 降级示意）：
      - 输入：
        ```
        case (sel) inside
          8'h00: y = a;
          [8'h10:8'h1f]: y = b;
          [8'h20 +/- 8'h03]: y = c;
          default: y = d;
        endcase
        ```
      - `values`（match 示例）：
        - `0: Symbol(sel)`
        - `1: Const(8'h00)`
        - `2: Op(kWildcardEq, [0,1])` (match0)
        - `3: Const(8'h10)`
        - `4: Const(8'h1f)`
        - `5: Op(kGe, [0,3])`
        - `6: Op(kLe, [0,4])`
        - `7: Op(kLogicAnd, [5,6])` (match1)
        - `8: Const(8'h20)`
        - `9: Const(8'h03)`
        - `10: Op(kSub, [8,9])`
        - `11: Op(kAdd, [8,9])`
        - `12: Op(kGe, [0,10])`
        - `13: Op(kLe, [0,11])`
        - `14: Op(kLogicAnd, [12,13])` (match2)
  - #### 循环语句（Pass5 静态展开）
    - repeat：
      - 输入：`repeat(3) y = a;`
      - 输出（示意）：
        - `values`：
          - `0: Symbol(a)` (rhs0)
        - `roots`：`[0]`
        - `writes`：
          - `WriteIntent{y, value=0, guard=invalid}`
          - `WriteIntent{y, value=0, guard=invalid}`
          - `WriteIntent{y, value=0, guard=invalid}`
    - for：
      - 输入：
        ```
        for (int i = 0; i < 2; i = i + 1) y = a;
        ```
      - 输出（示意）：
        - `values`：
          - `0: Symbol(a)` (rhs0)
        - `roots`：`[0]`
        - `writes`：
          - `WriteIntent{y, value=0, guard=invalid}`
          - `WriteIntent{y, value=0, guard=invalid}`
    - foreach：
      - 输入：
        ```
        foreach (arr[i]) y = a; // arr 有 2 个元素
        ```
      - 输出（示意）：
        - `values`：
          - `0: Symbol(a)` (rhs0)
        - `roots`：`[0]`
        - `writes`：
          - `WriteIntent{y, value=0, guard=invalid}`
          - `WriteIntent{y, value=0, guard=invalid}`
    - 说明：
      - 仅对静态可求值且总迭代次数不超过 `ConvertOptions.maxLoopIterations` 的循环做展开；
        默认上限为 65536；
        repeat/for/foreach 在不含 `break/continue` 时，超上限或不可静态求值 -> TODO + 单次访问回退；
        while/do-while/forever 不回退，直接报错；
        `guard=invalid` 表示无显式 guard（来自 `guardStack` 为空）。
      - 循环体内 `break/continue` 的 guard 可静态求值时，展开期直接裁剪：
        break 终止后续迭代，continue 跳过当前迭代剩余语句。
      - guard 不可静态求值时，启用动态 guard 传播：
        `loopAlive` 跨迭代传播，`flowGuard` 屏蔽当前迭代剩余语句；
        break/continue 分别更新 `loopAlive`/`flowGuard`。
      - while/do-while 使用 `EvalContext` 静态求值条件；do-while 至少执行一次；
        forever 需要在上限内由 break 终止，否则报错。
      - 若循环边界不可静态求值或超过上限 -> 报错不支持。
  - #### 循环语句（Pass5 动态 guard 传播）
    - 输入：
      ```
      for (int i = 0; i < 3; i++) begin
        if (stop) break;
        if (skip) continue;
        y = a;
      end
      ```
    - 处理要点（总）：
      - 动态 guard 由 `loopAlive` 与 `flowGuard` 组成；每次迭代开始 `flowGuard = loopAlive`。
      - break 更新 `loopAlive` 与 `flowGuard`，continue 只更新 `flowGuard`。
      - 写回 guard 由 `flowGuard && 分支 guard` 组合，保证 stop/skip 为真时屏蔽后续语句与迭代。
    - 输出（完整示意：values + writes）（分）：
      - 约定：`b_i = loopAlive_i`，`f_i = flowGuard_i`，每次迭代开始 `f_i = b_i`。
      - values（仅列 guard 相关新增节点，省略已存在的 symbol/const 节点）：
        - 迭代 0：
          - `t0 = f0 && stop`（break 触发条件）
          - `nb0 = !t0`
          - `f0a = f0 && nb0`（break 后 flowGuard）
          - `b1 = b0 && nb0`（下一迭代 loopAlive）
          - `t1 = f0a && skip`（continue 触发条件）
          - `nc0 = !t1`
          - `f0b = f0a && nc0`（continue 后 flowGuard，写回 guard）
        - 迭代 1：
          - `f1 = b1`
          - `t2 = f1 && stop`，`nb1 = !t2`，`f1a = f1 && nb1`，`b2 = b1 && nb1`
          - `t3 = f1a && skip`，`nc1 = !t3`，`f1b = f1a && nc1`
        - 迭代 2：
          - `f2 = b2`
          - `t4 = f2 && stop`，`nb2 = !t4`，`f2a = f2 && nb2`，`b3 = b2 && nb2`
          - `t5 = f2a && skip`，`nc2 = !t5`，`f2b = f2a && nc2`
      - writes（示意）：
        - `WriteIntent{target=y, value=rhs(a), guard=f0b, domain=comb}`
        - `WriteIntent{target=y, value=rhs(a), guard=f1b, domain=comb}`
        - `WriteIntent{target=y, value=rhs(a), guard=f2b, domain=comb}`
      - 说明：
        - 上述 `t*/nb*/nc*/f*` 均是 `LoweringPlan.values` 中的节点；
          `WriteIntent.guard` 保存其索引。
        - 实际节点索引由构建顺序决定，这里仅展示结构关系。
  - 生成与使用约定：
    - 生成：Pass4 扫描过程块与连续赋值，降级 RHS 并写入 `PlanArtifacts.loweringPlan`。
    - 使用：Pass5~Pass7 读取 `loweringPlan`，不回扫 AST。

### 3.11. WriteBackPlan
- 总览（总）：
  - 定位：记录写回合并结果（`updateCond + nextValue`），为后续 GraphAssembly
    将 sequential/latch 语义映射到 `kRegister/kLatch` 提供直接输入。
  - 生成：Pass6 WriteBackPass 从 `LoweringPlan.loweredStmts` 合并得到。
  - 作用边界：只处理完整信号写回（无 LHS slices）；切片写回在此阶段报 TODO 并跳过。

- 字段结构（分）：
  - WriteBackPlan：
    - `entries`: `std::vector<Entry>` -> 写回合并条目列表（按 target + domain 分组）。
  - WriteBackPlan.Entry：
    - `target`: `PlanSymbolId` -> 写回目标符号。
    - `signal`: `SignalId` -> 目标信号索引（未映射时为 invalid）。
    - `domain`: `ControlDomain` -> comb/seq/latch/unknown。
    - `updateCond`: `ExprNodeId` -> 写回触发条件（guard OR 合并）。
    - `nextValue`: `ExprNodeId` -> 合并后的 nextValue（按语句顺序构建 mux）。
    - `eventEdges`: `std::vector<EventEdge>` -> 顺序域的事件边沿列表。
    - `eventOperands`: `std::vector<ExprNodeId>` -> 顺序域事件信号表达式列表。
    - `location`: `slang::SourceLocation` -> 首条写回语句位置（诊断锚点）。

- 合并规则（分）：
  - 分组键：`target + domain + eventEdges + eventOperands`。
  - Guard 合并：
    - 每条 Write 的 guard 缺失时视作常量 `1'b1`。
    - `updateCond = OR(guard_i)`（按写回出现顺序聚合）。
  - NextValue 合并：
    - 初始化 `next = oldValue`（`oldValue` 即目标信号本身）。
    - 依语句顺序构建 mux 链：`next = mux(guard_i, value_i, next)`。
  - 顺序域约束：
    - 缺少 edge-sensitive 事件列表 -> 发出 warning 并跳过该组。
  - 锁存域约束：
    - 事件列表为空，仅依赖 `updateCond/nextValue`。

- 案例讲解（分）：
  - 顺序写回（reset/enable 优先级）：
    - 输入：
      ```
      always_ff @(posedge clk) begin
        if (rst) q <= 1'b0;
        else if (en) q <= d;
      end
      ```
    - 合并（示意）：
      - guards：`g0 = rst`，`g1 = (!rst) && en`。
      - `updateCond = g0 || g1`
      - `nextValue = mux(g0, 1'b0, mux(g1, d, q))`
      - `eventEdges = [posedge]`，`eventOperands = [clk]`
    - 说明：先写的 reset guard 优先级更高，mux 链保持语句顺序。
  - 锁存写回：
    - 输入：
      ```
      always_latch if (en) q <= d;
      ```
    - 合并（示意）：
      - `updateCond = en`
      - `nextValue = mux(en, d, q)`
      - `eventEdges/eventOperands` 为空

### 3.12. Diagnostics / Logger
- 总览：
  - `ConvertDiagnostics`：统一收集 todo/error/warn，保留源码位置信息。
  - `ConvertLogger`：level/tag 过滤的可控日志接口。

## 4. Pass 中间数据结构
- 总览：
  - 定位：单个 Pass 内部的临时结构/索引/去重表，不进入 `ModulePlan` 或 `PlanArtifacts`。
  - 目的：降低跨 Pass 的耦合与状态污染，便于并行与复用。
- 归属（Pass -> 中间结构）：
  - Pass1：ParameterSnapshot
  - Pass2：TypeResolution / PortIndexMap / SignalIndexMap
  - Pass3：RWAnalyzerState / RWVisitor
  - Pass5：StmtLowererState（guardStack/flowStack/loopFlowStack 等临时状态）

### 4.1. Pass1: SymbolCollector
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

### 4.2. Pass2: TypeResolver
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
      - `unpackedDims`: `std::vector<UnpackedDimInfo>` -> unpacked 维度（外到内，含 left/right/extent）。
    - 字段详解：
      - `width/isSigned`：写回端口与信号的最终基本类型属性。
      - `packedDims/unpackedDims/memoryRows`：支撑 memory 判定与维度/方向保留。
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

### 4.3. Pass3: RWAnalyzer
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

### 4.4. Pass5: StmtLowerer 内部临时结构
- 总览（总）：
  - 定位：仅在 Pass5 运行期使用的临时状态，服务于语句递归、guard 组合与 loop 控制流传播。
  - 目标：不新增持久化字段的前提下，完成 if/case/loop 的 guard 叠加与 break/continue 的语义还原。
  - 代码位置：`StmtLowererState`（定义），`StmtLowererState::visitStatement`（主入口）。
- 结构清单（分）：
  - `guardStack`：分支路径 guard 栈（if/case 的 path guard）。
  - `flowStack`：迭代内 flow guard 栈（动态 break/continue 使用）。
  - `loopFlowStack`：循环上下文栈，保存当前循环的 `loopAlive`。
  - `nextRoot/nextTemp`：消费 RHS root 与分配临时符号的游标。
  - `loopControlFailure`：静态展开失败原因（诊断信息来源）。
- 数据落位（分）：
  - `guardStack/flowStack/loopFlowStack` 只存在于 Pass5，退出 Pass5 后即销毁。
  - 最终 guard 落位在 `LoweringPlan.values`，`WriteIntent.guard` 仅保存索引，不保存 stack 本体。
  - `loopAlive/flowGuard` 本身也是普通表达式节点，和其它 guard 一样进入 `LoweringPlan.values`。
- 运行机制（分）：
  - `guardStack`：进入 if/case 分支时 push，离开分支时 pop。
  - `flowStack`：进入动态展开的“迭代体”时 push，break/continue 更新栈顶 guard。
  - `loopFlowStack`：进入动态循环时 push，break 更新 `loopAlive`，用于后续迭代 guard。
  - 代码位置：`StmtLowererState::pushGuard/popGuard`，`pushFlowGuard/popFlowGuard`，
    `pushLoopContext/popLoopContext`，`handleLoopBreak/handleLoopContinue`。

#### 4.4.1. 动态 break/continue 完整示例（分 + 案例）
- 输入：
  ```
  for (int i = 0; i < 3; i++) begin
    if (stop) break;
    if (skip) continue;
    y = a;
  end
  ```
- 总体流程（总）：
  - 用 `loopAlive` 记录“后续迭代是否仍然可达”，用 `flowGuard` 记录“当前迭代内是否继续执行后续语句”。
  - break 更新 `loopAlive` 与 `flowGuard`，continue 只更新 `flowGuard`。
  - 最终写回的 guard = `flowGuard && pathGuard`，并写入 `LoweringPlan.values` 供 `WriteIntent.guard` 引用。
- 初始数据结构（分）：
  - `guardStack = []`（无分支路径 guard）。
  - `flowStack = []`（尚未进入迭代体）。
  - `loopFlowStack = []`（尚未进入动态循环）。
  - `LoweringPlan.values` 中尚无与该循环相关的 guard 节点。
- 进入循环与迭代体（分）：
  - 进入动态循环时 `pushLoopContext`，得到 `loopAlive = 1` 并存入 `loopFlowStack`。
  - 每次迭代开始：`pushFlowGuard(loopAlive)`，此时 `flowStack.top == loopAlive`。
- 处理 `if (stop) break;`（分）：
  - `if (stop)` 进入 true 分支时 `guardStack.top == stop`。
  - `handleLoopBreak` 使用 `currentGuard = flowGuard && stop` 作为触发条件：
    - `notTrigger = !(flowGuard && stop)`
    - `flowGuard = flowGuard && notTrigger`（屏蔽本迭代后续语句）
    - `loopAlive = loopAlive && notTrigger`（屏蔽后续迭代）
  - 直观等价于：`flowGuard = flowGuard && !stop`，`loopAlive = loopAlive && !stop`。
- 处理 `if (skip) continue;`（分）：
  - `if (skip)` 进入 true 分支时 `guardStack.top == skip`。
  - `handleLoopContinue` 使用 `currentGuard = flowGuard && skip`：
    - `notTrigger = !(flowGuard && skip)`
    - `flowGuard = flowGuard && notTrigger`
  - 直观等价于：`flowGuard = flowGuard && !skip`（仅影响当前迭代）。
- 最终写回落地（分）：
  - 语句 `y = a` 的写回 guard 使用 `currentGuard = flowGuard && pathGuard`，
    在本例中 `pathGuard` 为空，因此写回 guard 直接来自 `flowGuard`。
  - `flowGuard`/`loopAlive` 相关的 `kLogicAnd/kLogicNot` 节点追加进
    `LoweringPlan.values`，其索引由 `WriteIntent.guard` 引用。

### 4.5. Pass8: GraphAssembly 临时结构
- 总览：
  - 定位：仅在 GraphAssembly 构建期存在的缓存与索引表。
  - 目的：将 `PlanSymbolId/ExprNodeId` 映射到 Graph 的 `ValueId/OperationId`。
- 主要缓存：
  - `symbolIds`: `PlanSymbolId -> GraphSymbolId` 缓存，避免重复驻留字符串。
  - `valueBySymbol`: `PlanSymbolId -> ValueId`，端口/信号 Value 的快速查找表。
  - `valueByExpr`: `ExprNodeId -> ValueId`，表达式节点的结果缓存。
  - `memoryReadIndexByExpr`: `ExprNodeId -> MemoryReadPort` 索引缓存，用于拦截 memory 读表达式。
- 使用方式：
  - 端口与信号先创建 Value 并填充 `valueBySymbol`。
  - `emitExpr` 递归发射 ExprNode 并缓存 `valueByExpr`。
  - `WriteBackPlan` 直接使用缓存生成 `kAssign/kRegister/kLatch`。

### 4.6. Pass8: GraphAssembly 数据抽取视角
- 总览：
  - 关注点：从 `ModulePlan/LoweringPlan/WriteBackPlan` 中抽取必要信息，形成 Graph 组装输入。
- 关键抽取点（分）：
  - 端口与信号（来自 `ModulePlan`）：
    - `ModulePlan.ports` -> 端口方向/位宽/符号名；
    - `PortInfo.inoutSymbol` -> inout 三值（__in/__out/__oe）绑定名；
    - `ModulePlan.signals` -> 非 memory 信号的宽度/签名与名称。
  - 表达式节点（来自 `LoweringPlan`）：
    - `LoweringPlan.values` -> ExprNode 图；
    - `ExprNode.kind/op/operands/widthHint` -> op 类型、操作数索引、结果宽度；
    - `ExprNode.symbol/tempSymbol` -> 命名 Value 的 symbol 源。
  - 写回条目（来自 `WriteBackPlan`）：
    - `WriteBackPlan.entries` -> 目标符号、控制域、updateCond、nextValue；
    - `Entry.eventEdges/eventOperands` -> 顺序域触发信息；
    - `Entry.signal` -> 目标信号索引（用于过滤 memory/非法目标）。
  - Memory 端口（来自 `LoweringPlan`/`ModulePlan`）：
    - `LoweringPlan.memoryReads/memoryWrites` -> `kMemoryReadPort/kMemoryWritePort`；
    - `MemoryReadPort.isSync` -> 同步读额外生成 `kRegister`；
    - `ModulePlan.signals` 中 `memoryRows/width/isSigned` -> `kMemory` 属性。
  - 顶层标记（来自 `ConvertContext.root`）：
    - `RootSymbol.topInstances` -> 顶层 PlanKey 集合，用于 `topGraphs` 标记。

## 5. Pass 与数据结构关系图
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
  +--> Pass6: WriteBackPass       -> WriteBackPlan
  |
  +--> Pass7: MemoryPortLowererPass -> refine LoweringPlan (mem ports)
  |                               -> PlanCache[PlanKey].artifacts.writeBackPlan
  |
  +--> Pass8: GraphAssemblyPass   -> Graph (GRH Op/Value) -> Netlist
```

## 6. 并行化策略（以模块为粒度）
### 6.1. 并行边界
- 一个参数特化模块对应一个任务流水线。
- Pass1~Pass7 可并行执行；Pass8 串行写入 `Netlist`。

### 6.2. 任务与调度
- 任务 key：`PlanKey = body + paramSignature`。
- 调度：固定线程池 + 任务队列；主线程负责投递与汇总。
- 任务发现：在 Pass1 遍历实例时，将子模块的 `PlanKey` 投递到队列。

### 6.3. 去重与同步
- `PlanCache` 负责去重，保证同一 `PlanKey` 只处理一次。
- `PlanTaskQueue` 负责任务分发，支持队列关闭以收敛并行阶段。

### 6.4. 数据一致性
- 并行阶段只写 `PlanCache/PlanArtifacts`，不改 `Netlist`。
- `GraphAssemblyPass` 串行落地 Graph 与 alias，避免并发写冲突。

### 6.5. 诊断与日志
- 诊断：线程本地缓存，最终由主线程合并。
- 日志：`ConvertLogger` 统一输出，避免多线程交错。

## 7. GRH 输出契约
- 端口与信号宽度、符号属性完全基于 slang 类型系统。
- memory 仅使用精简后的读写端口集合（`kMemoryAsyncReadPort` / `kMemorySyncReadPort` / `kMemorySyncReadPortRst` / `kMemorySyncReadPortArst` / `kMemoryWritePort` / `kMemoryMaskWritePort`）。
- 读端口复位仅作用于读寄存器，写端口无复位语义。
- 四态逻辑语义保持一致，常量允许 x/z。
- Netlist 允许多个顶层模块，Convert 标记 `topInstances` 形成 topGraphs 列表。

## 8. 代码骨架落位
- `include/convert.hpp`：Convert 核心数据结构与接口声明。
- `src/convert.cpp`：Convert 骨架实现与默认行为。
- `CMakeLists.txt`：新增 `convert` 静态库目标。
