# Convert 工作流（动态视角）

> 本文描述 Convert 的运行时流程，强调 Pass 顺序、功能、输入与输出。

## 概念速览（本文内使用）
- `RootSymbol`：slang AST 的设计根节点，提供 `topInstances` 与 `Compilation`。
- `ConvertDriver`/`ConvertOptions`：转换入口与配置，负责创建 ctx 并控制日志/诊断策略。
- `ConvertContext`（简称 ctx）：贯穿转换流程的共享状态，包含 diagnostics/logger/cache/queue 等指针。
- `PlanKey`：`InstanceBodySymbol + paramSignature` 的组合，用于唯一标识参数特化模块；`paramSignature` 为非 localparam 的参数求值序列化标识。
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
- 当前实现已完成 Context Setup、Pass1、Pass2 与 Pass3，仍返回空 `Netlist`，后续 Pass 逻辑待接入。

## Pass1: SymbolCollectorPass
- 功能：收集端口/信号/实例信息，形成 ModulePlan 骨架，并发现子模块任务。
- 输入：`InstanceBodySymbol` + ctx。
- 输出形式：`PlanCache[PlanKey].plan`（包含 ports/signals/instances）。
- 运行步骤：
  - 初始化 `ModulePlan`：设置 `plan.body`，根据 `body.name`/`definition.name` 生成 `plan.moduleSymbol`。
  - 端口收集：
    - 遍历 `body.getPortList()`，只处理 `PortSymbol`，并记录 `PortInfo{symbol,direction}`。
    - inout 通过 `PortInfo.inoutSymbol` 记录 `__in/__out/__oe` 名称；`Ref` 方向与匿名/空端口报告 error。
    - `MultiPortSymbol`/`InterfacePortSymbol` 当前视为不支持并记录 error。
  - 信号收集：
    - 扫描 `body.members()` 中的 `NetSymbol`/`VariableSymbol`。
    - Pass1 仅填 `SignalInfo{symbol,kind}`，宽度/维度由 Pass2 补齐。
    - 匿名符号跳过并记录 warn。
  - 实例收集与任务发现：
    - 扫描 `InstanceSymbol`/`InstanceArraySymbol`/`GenerateBlockSymbol`/`GenerateBlockArraySymbol`。
    - 记录 `InstanceInfo{instanceSymbol,moduleSymbol,isBlackbox}`；黑盒实例额外记录参数绑定。
    - 为每个子实例体投递 `PlanKey{body}` 到 `PlanTaskQueue`。
- 当前实现由 `ConvertDriver::convert` 先投递顶层实例，再 drain 队列并写入 `PlanCache`。

## Pass2: TypeResolverPass
- 功能：计算位宽、签名、packed/unpacked 维度与 memory 行数。
- 输入：`ModulePlan` + `slang::ast::Type` + ctx。
- 输出形式：原地更新 `ModulePlan`。
- 运行步骤：
  - 预处理：根据 `PlanSymbolTable` 建立端口/信号名到索引的映射表。
  - 端口：回扫 `PortSymbol` 获取类型，剥离 type alias 并计算固定宽度，
    填充 `PortInfo.width/isSigned`；端口出现 unpacked 维度时记录 warn 并忽略其数组维度。
  - 信号：回扫 `NetSymbol`/`VariableSymbol` 获取类型，剥离 type alias；
    若存在 fixed unpacked 维度则按外到内顺序记录 `unpackedDims`，
    同时累乘写入 `memoryRows`，并继续下探到元素类型。
  - packed 维度：在元素类型上收集 packed array 维度并写入 `packedDims`。
  - 宽度/签名：对最终元素类型计算固定宽度并写入 `SignalInfo.width/isSigned`。
  - 约束：不定宽度或动态/关联/队列数组会记录诊断并回退为 1-bit。
  - 边界：宽度与维度在超出 GRH 表示范围时 clamp 到上限。

## Pass3: RWAnalyzerPass
- 功能：建立读写关系、控制域语义，并记录访问点用于端口数量/优先级分析。
- 输入：`ModulePlan` + 过程块/连续赋值 AST + ctx。
- 输出形式：原地更新 `ModulePlan.rwOps`/`ModulePlan.memPorts`，并写入 `RWOp.sites`/`MemoryPortInfo.sites`。
- 运行步骤：
  - 预处理：建立 `PlanSymbolId -> SignalId` 映射，清空 `rwOps`/`memPorts`，初始化 `RWAnalyzerState`（每模块一个）。
  - 过程块扫描：遍历 `ProceduralBlockSymbol`，根据 `procedureKind` 与 timing control
    分类 `ControlDomain`（comb/seq/latch/unknown）。
  - 连续赋值扫描：遍历 `ContinuousAssignSymbol`，固定为 combinational 域。
  - 语句遍历：
    - assignment expression：LHS 递归标记为写，RHS/条件/索引标记为读。
    - pre/post increment/decrement：同一目标同时记读与写。
    - 其余表达式按读取路径遍历。
  - 访问点记录：每次访问追加 `AccessSite{location, sequence}`；
    `location` 取 `expr.sourceRange.start()`，不直接保存 AST 节点。
  - Memory 端口生成：对 `SignalInfo.memoryRows > 0` 的访问生成 `MemoryPortInfo`；
    sequential 域推断 `isSync=true`，comb 域为 `false`。
  - 归并：按 `(signal, domain, isWrite)` 与 `(memory, flags)` 归并形态，
    重复访问不丢弃，改为追加 `sites`。

## Pass4: ExprLowererPass
- 功能：将 RHS 表达式树归一化为 LoweringPlan 中的表达式节点图。
- 输入：`ModulePlan` + 表达式 AST + ctx。
- 输出形式：`PlanCache[PlanKey].artifacts.loweringPlan`。
- 形式化定义：
  - LoweringPlan.values：表达式节点序列，节点类型为 `ExprNode{kind, op, operands, symbol, literal, location}`。
  - LoweringPlan.roots：每条 RHS 表达式对应一个 root，指向其顶层 `ExprNodeId`。
  - LoweringPlan.tempSymbols：每个 `Operation` 节点分配一个临时名 `_expr_tmp_<n>`。
- 处理规则：
  - 输入集合：模块内全部 `AssignmentExpression` 的 RHS；复合赋值 `lhs op= rhs` 转为
    `op(lhs, rhs)` 后作为 RHS root。
  - 终结节点：
    - Named/Hierarchical value -> `ExprNode{kind=Symbol, symbol=PlanSymbolId}`。
    - Integer/unsized literal/string -> `ExprNode{kind=Constant, literal=...}`。
  - 复合节点：
    - Unary/Binary/Conditional/Concat/Replicate/Select -> `ExprNode{kind=Operation, op=...}`，
      `operands` 按源 AST 的子表达式顺序填入其 `ExprNodeId`。
  - 失败策略：不支持的表达式记录 TODO 诊断并跳过该子树。
- 案例：
  - 输入：`assign y = (a & b) ? ~c : (a | b);`
  - 输出（示意）：
    - values：
      `Symbol(a)`, `Symbol(b)`, `Op(kAnd, [a,b])`,
      `Symbol(c)`, `Op(kNot, [c])`, `Symbol(a)`, `Symbol(b)`,
      `Op(kOr, [a,b])`, `Op(kMux, [and, not, or])`
    - roots：`[kMux 节点索引]`
    - tempSymbols：`[_expr_tmp_0, _expr_tmp_1, _expr_tmp_2, _expr_tmp_3]`

## Pass5: StmtLowererPass
- 功能：语句与控制流降级，形成 guard 与写回意图。
- 输入：`ModulePlan` + 语句 AST + `LoweringPlan` + ctx。
- 输出形式：更新 `LoweringPlan`。
- 顶层流程：
  - 初始化 Pass5 状态（清空 `writes`，准备 guard 栈与 root 游标）。
  - 遍历模块成员并递归遍历语句树，形成 guard 与写回意图。
  - 将 guard 表达式节点写入 `LoweringPlan.values`，并追加 `WriteIntent`。
- 步骤分解：
  - 步骤 1：初始化与状态准备
    - 清空 `LoweringPlan.writes`。
    - 初始化 `guardStack`、`domain`、`nextRoot` 等状态。
  - 步骤 2：模块成员遍历
    - `ContinuousAssignSymbol`：设定 `domain=comb`，访问赋值表达式。
    - `ProceduralBlockSymbol`：按过程块类型分类 `domain`，递归访问语句体。
    - `GenerateBlockSymbol/GenerateBlockArraySymbol`：递归进入成员。
  - 步骤 3：语句级递归遍历
    - `StatementList/BlockStatement/TimedStatement/InvalidStatement`：递归进入子语句。
    - `ExpressionStatement/ProceduralAssignStatement`：
      - 提取 `AssignmentExpression`，消费 `LoweringPlan.roots` 对应 RHS；
      - 读取当前 guard 与 domain，追加 `WriteIntent`。
    - `ConditionalStatement`：
      - 对多条件 `if` 链构建 `cond`（条件间 `logic-and`）；
      - true guard = `base && cond`，false guard = `base && !cond`；
      - 推入 guard 后递归访问分支语句。
    - `CaseStatement`：
      - 构建每个 item 的 match，并以 `logic-or` 聚合；
      - guard = `base && match && !priorMatch`；
      - default guard = `base && !priorMatch`；
      - 判定规则：
        - 普通 `case`：若 control 为 2-state 且 item 可常量求值且不含 X/Z -> `kEq`；
          否则回退为 `kCaseEq` 并发出 warning（可能不可综合）。
        - `casez/casex`：若 item 为常量，先生成 mask（casez 忽略 Z/?，casex 忽略 X/Z/?），
          使用 `(control & mask) == (item & mask)` 生成 match（综合优先）；
          item 非常量无法生成 mask 则回退 `kCaseEq` 并 warning。
    - 循环语句：
      - `repeat/for/foreach` 若可静态求值则展开多次访问 body；
      - 不可静态求值则记录 TODO 并按一次访问回退。
  - 步骤 4：guard 表达式降级
    - guard 使用 Pass5 内部表达式降级逻辑追加到 `LoweringPlan.values`；
    - guard 节点同样分配 `_expr_tmp_<n>` 临时名。
    - 案例（if/else guard）：
      - 输入：
        ```
        always_comb begin
          if (a) y = b; else y = c;
        end
        ```
      - guard 生成过程：
        - 当前 guard 为空 -> base = invalid
        - 条件 `a` 降级为 `Symbol(a)`，记为 `g0`
        - true guard = `g0`，false guard = `!g0`
      - `LoweringPlan.values`（示意）：
        - `0: Symbol(b)` (rhs0)
        - `1: Symbol(c)` (rhs1)
        - `2: Symbol(a)` (g0)
        - `3: Op(kLogicNot, [2])` (g1)
      - `LoweringPlan.writes`：
        - `WriteIntent{target=y, value=0, guard=2}`
        - `WriteIntent{target=y, value=1, guard=3}`
  - 步骤 5：写回意图生成
    - 追加 `WriteIntent{target, value, guard, domain, isNonBlocking, location}`。

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
