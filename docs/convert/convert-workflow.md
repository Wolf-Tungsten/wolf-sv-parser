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
- `LoweringPlan`/`WriteBackPlan`：Pass4/Pass6 产出并缓存到 `PlanArtifacts` 的中间计划。
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
- 当前实现已完成 Context Setup、Pass1~Pass6，仍返回空 `Netlist`，Pass7/Pass8 待接入。

## CLI 主流程（wolf-sv-parser）
- 入口：`src/main.cpp` 负责解析 CLI 参数并驱动 Convert/Transform/Emit。
- 关键步骤：
  - 根据 `--convert-log/--convert-log-level/--convert-log-tag` 配置 `ConvertDriver` 日志。
  - 调用 `ConvertDriver::convert` 生成 `Netlist`，记录诊断并在 error 时停止。
  - 执行 transform passes（const-fold + stats）。
  - 根据 `--emit-json/--emit-sv` 选择输出 JSON/SV；`-o/--emit-out-dir` 控制路径。
- HDLBits 流程：`make run_hdlbits_test` 会调用 `wolf-sv-parser --emit-sv --emit-json -o <dut>.v`，
  产物写入 `build/hdlbits/<DUT>/` 并供 Verilator 使用。

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
- 功能：语句与控制流降级，生成 guard 与写回意图。
- 输入：`ModulePlan` + 语句 AST + `LoweringPlan` + ctx。
- 输出形式：更新 `LoweringPlan`（追加 guard 节点、`WriteIntent` 与 `LoweredStmt`）。

### 0. 总体脉络
Pass5 以“入口类型”为一级结构：
1) 连续赋值入口（ContinuousAssignSymbol）  
2) 过程块入口（ProceduralBlockSymbol）  
3) 生成器入口（GenerateBlockSymbol / GenerateBlockArraySymbol）  
每个入口内部再细化到具体语句类型的处理流程。

### 1. 连续赋值入口（ContinuousAssignSymbol）
#### 1.1 入口流程
1) 设置 `domain = comb`。  
2) 访问赋值表达式并消费 RHS root（来自 Pass4）。  
3) 解析 LHS -> `targets + slices`（支持 concat/streaming concat、bit/range/member select）。  
4) 若 LHS 为复合目标则按位宽拆分 RHS 并生成多条 `WriteIntent`；否则生成单条。  
5) 右到左 streaming（slice_size > 0）与 with-clause 当前仍报 TODO。  
代码位置：`lowerStmtContinuousAssign`。  

#### 1.2 示例
```
assign y = a & b;
```
- 输出：`WriteIntent{target=y, value=rhs(a&b), guard=invalid, domain=comb}`。  

### 2. 过程块入口（ProceduralBlockSymbol）
#### 2.1 入口流程
1) 根据过程块类型设置 `domain`：  
   - `always_comb` -> comb  
   - `always_ff` -> seq  
   - `always_latch` -> latch  
   - `initial` / `final` -> seq  
   - `always` -> 进一步按 timing control 判定：  
     - 无 timing control 或 implicit event（`@*` 语义）-> comb  
     - 包含边沿敏感事件（`posedge/negedge`）-> seq  
     - 纯电平敏感 event list -> comb  
     - 其余无法识别的 timing -> unknown  
2) 递归访问语句体。  
3) 退出时恢复原 `domain`。  
代码位置：`classifyProceduralBlock`，`lowerStmtProceduralBlock`。  

#### 2.2 过程块内语句类型细分
过程块内部的语句递归主要覆盖以下类型：

##### 2.2.1 赋值类语句
- `ExpressionStatement/ProceduralAssignStatement`：  
  - 消费 RHS root；解析 LHS（支持 concat/streaming/member）；生成 `WriteIntent`（复合 LHS 拆分）。  
代码位置：`StmtLowererState::visitStatement`，`StmtLowererState::handleAssignment`。  

示例：  
```
always_ff @(posedge clk) begin
  q <= d;
end
```
- 输出：`WriteIntent{target=q, value=rhs(d), guard=invalid, domain=seq}`。  

##### 2.2.2 分支语句（if/else）
- 多条件 if：按顺序 `logic-and` 合并 `cond`。  
- `trueGuard = base && cond`；`falseGuard = base && !cond`。  
- guard 入栈后递归访问分支语句。  
- pattern condition 直接报错并跳过分支。  
代码位置：`StmtLowererState::visitConditional`。  

示例：  
```
if (a) y = b; else y = c;
```
- 写回：`guard=g0` 与 `guard=!g0` 两条。  

##### 2.2.3 分支语句（case / casez / casex / case inside）
- item guard = `base && match && !priorMatch`；default guard = `base && !priorMatch`。  
- `case/casez/casex`：  
  - 先按二值逻辑评估分支覆盖：若二值覆盖完整或存在 default -> 使用二值比较（`kEq` / mask+`kEq`），并标记为组合完整覆盖。  
  - 二值覆盖不完整或无法按二值降低（例如 casez/casex item 非常量） -> 使用 `kCaseEq`（或 mask+`kCaseEq`）保留四值语义，并给出 warning。  
- `case inside`：  
  - `ValueRange`：`[a:b]` / `[a +/- b]` / `[a +%- b]` 按范围匹配公式生成。  
  - 普通表达式：integral -> `kWildcardEq`，否则 `kEq`。  
代码位置：`StmtLowererState::visitCase`，`StmtLowererState::buildCaseItemMatch`，`StmtLowererState::buildCaseWildcardMask`，`StmtLowererState::buildInsideValueRangeMatch`。  

示例（casez mask）：  
```
casez (sel)
  2'b0?: y = a;
  default: y = b;
endcase
```
- `match0 = (sel & mask) == (2'b0? & mask)`，default guard 为 `!match0`。  

##### 2.2.4 循环语句（repeat / for / foreach / while / do-while / forever）
- 可静态求值且迭代次数 <= `ConvertOptions.maxLoopIterations` 时展开。  
- `break/continue` 的处理分两种：  
  - guard 可静态求值：直接在展开期裁剪迭代（break 终止后续迭代，continue 跳过当前迭代剩余语句）。  
  - guard 不可静态求值：进入动态 guard 传播模式（仅 repeat/for/foreach）：  
    - `loopAlive`：跨迭代 guard；break 更新 `loopAlive = loopAlive && !breakCond`。  
    - `flowGuard`：当前迭代 guard；break/continue 更新 `flowGuard = flowGuard && !cond`，用于屏蔽剩余语句。  
- 若循环边界不可静态求值或超过上限 -> 报错不支持。  
- repeat/for/foreach：不含 `break/continue` 时，不可静态求值或超上限 -> TODO + 单次访问回退。  
- while/do-while/forever：仅静态展开，do-while 保证至少执行一次，forever 需在上限内由 break 终止。  
代码位置：`StmtLowererState::visitStatement`，`StmtLowererState::tryUnrollRepeat`，
`StmtLowererState::tryUnrollFor`，`StmtLowererState::tryUnrollForeach`，
`StmtLowererState::tryUnrollWhile`，`StmtLowererState::tryUnrollDoWhile`，
`StmtLowererState::tryUnrollForever`，`StmtLowererState::visitStatementWithControl`。  

示例（for + continue）：  
```
for (int i = 0; i < 4; i++) begin
  if (i == 1) continue;
  y = a;
end
```
- 结果：展开写回 3 次（跳过 `i==1`）。  

示例（动态 break/continue guard 传播）：  
```
for (int i = 0; i < 3; i++) begin
  if (stop) break;
  if (skip) continue;
  y = a;
end
```
- 解释：  
  - 初始 `loopAlive = 1`，每次迭代开始 `flowGuard = loopAlive`。  
  - `break`：`loopAlive = loopAlive && !stop`，同时 `flowGuard = flowGuard && !stop`。  
  - `continue`：`flowGuard = flowGuard && !skip`。  
  - 写回 guard = `flowGuard && 当前分支 guard`，保证 stop/skip 为真时屏蔽后续语句与迭代。  

##### 2.2.5 调试系统任务与 DPI 调用
- `ExpressionStatement` 中识别调试系统任务与 DPI 调用，按语句顺序写入 `LoweredStmt`。  
- 调试系统任务：  
  - `$display/$write/$strobe` -> `LoweredStmt(Display)`，携带 `formatString/args`。  
  - `$info/$warning/$error/$fatal` -> `LoweredStmt(Assert)`，携带 `message/severity`。  
- DPI 调用：  
  - 仅识别 `DPI-C import function` 的语句调用（input/output 方向）。  
  - `in` -> `ExprNodeId`，`out` -> `results`，可选返回值写入 `results[0]` 且 `hasReturn=true`。  
  - 同步记录 `DpiImportInfo` 到 `LoweringPlan.dpiImports`，按 symbol 去重并校验签名一致。  
- 事件绑定：进入过程块时解析 edge-sensitive timing control（`posedge/negedge`）；  
  若无法解析或非 edge-sensitive，display/DPI 调用丢弃并产生诊断。  
- 参数表达式复用 Pass4/Pass5 的 `lowerExpression` 降级为 `ExprNodeId`。  

##### 2.2.6 不支持语句
- `PatternCaseStatement`：报错并跳过。  
- `TimedStatement` / `wait` / `wait fork` / `wait order` / `event trigger` / `disable fork`：
  发出 warning，忽略 timing/并发语义，仅降低可执行的 body（若存在）。  
代码位置：`StmtLowererState::visitStatement`。  

### 3. 生成器入口（GenerateBlockSymbol / GenerateBlockArraySymbol）
#### 3.1 入口流程
1) 跳过未实例化 block。  
2) 遍历成员，对每个成员再次执行入口分派（1/2/3）。  
3) 遇到嵌套 generate 继续递归。  
代码位置：`lowerStmtMemberSymbol`，`lowerStmtGenerateBlock`，`lowerStmtGenerateBlockArray`。  

#### 3.2 示例
```
generate
  if (USE_A) begin : g
    assign y = a;
  end
endgenerate
```
- 输出：若实例化，等价于一次 ContinuousAssign 入口处理。  

### 4. 写回意图落地
- 形态：`WriteIntent{target, slices, value, guard, domain, isNonBlocking, coversAllTwoState, location}`（`slices` 支持 member select）。  
- `guard = kInvalidPlanIndex` 表示无显式 guard。  
代码位置：`StmtLowererState::handleAssignment`。  

## Pass6: WriteBackPass
- 功能：合并写回序列，生成 `updateCond + nextValue`，并为顺序/锁存域标注事件绑定。
- 输入：`ModulePlan` + `LoweringPlan` + ctx。
- 输出形式：`PlanCache[PlanKey].artifacts.writeBackPlan`。
- 处理要点：
  - 无切片写回从首条写入值起步，后续语句按顺序用 `mux(guard, value, next)` 连接；
    不再强制引入 `oldValue` 兜底，避免组合写回生成自引用 MUX。
  - 组合域若不存在无条件写回（guard 恒真），WriteBack 会将该组合组提升为 latch 语义，
    由 `updateCond` 负责保持旧值。
  - 若写回来自二值覆盖完整或带 default 的 case 且处于无条件 guard 下，会视为覆盖完整，
    保持组合域不降为 latch。
  - 对 LHS 带 slices 的写回执行 read/modify/write：
    - 静态 bit/range/member select：拆分为 `kSliceDynamic` + `kConcat` 组合的新值。
    - 动态 bit/part-select：生成 mask/shift 组合并保留诊断。
  - 合并顺序严格按语句顺序，后出现的写回优先级更高。

### 案例讲解（基于核心数据结构的转换）
示例输入（SystemVerilog）：
```
module write_back_seq(
    input  logic clk,
    input  logic en,
    input  logic d,
    output logic q
);
    always_ff @(posedge clk) begin
        if (en) q <= d;
    end
endmodule
```

给定（示意数据）：
- `LoweringPlan.values`：
  - `v0 = Symbol(d)`
  - `v1 = Symbol(en)`
  - `v2 = Symbol(clk)`
- `LoweringPlan.loweredStmts`（Write 语句节选）：
  - `LoweredStmt{kind=Write, updateCond=v1, eventEdges=[posedge], eventOperands=[v2],`
    `write.target=q, write.value=v0, write.guard=v1, write.domain=seq}`

输出（示意）：
- `WriteBackPlan.entries`：
  - `Entry{target=q, domain=seq, updateCond=v1, nextValue=v0,`
    `eventEdges=[posedge], eventOperands=[v2]}`

## Pass7: MemoryPortLowererPass
- 功能：细化 memory 读写端口的 Lowering 描述，输出 read/write 端口条目。
- 输入：`ModulePlan.memPorts` + `LoweringPlan` + ctx。
- 输出形式：更新 `LoweringPlan.memoryReads/memoryWrites`。
- 处理要点：
  - 读端口：识别表达式中的 `mem[addr]`，组合域生成读端口；顺序域要求 edge-sensitive 事件绑定。
  - 顺序读：记录 `updateCond` 作为 enable 入口（无 guard 时为常量 1）。
  - 写端口：从写回语句识别 `mem[addr]` 写入，生成 `updateCond/data/mask`；
    若为 bit/range 写入则生成 mask（全写时 mask=全 1）。
  - 多维 memory：使用 unpackedDims 线性化地址索引。
    线性化前按声明范围修正索引（升序 `idx-left`，降序 `left-idx`）。
  - Indexed part-select 的 width 必须为可静态求值的常量（含模块/包参数、条件表达式与拼接复制）；
    拼接/复制支持嵌套算术，宽度取决于表达式的宽度提示。
    可静态求值的越界写入会给出诊断并跳过，base 为动态表达式时给出 warning 并继续生成。
  - 顺序域缺失 edge-sensitive 事件则诊断并跳过该端口。

### 案例讲解（基于核心数据结构的转换）
示例输入（SystemVerilog）：
```
module mem_port_basic(
    input  logic clk,
    input  logic en,
    input  logic [3:0] addr,
    input  logic [7:0] wdata,
    output logic [7:0] rdata
);
    logic [7:0] mem [0:15];
    always_ff @(posedge clk) if (en) mem[addr] <= wdata;
    assign rdata = mem[addr];
endmodule
```

给定（示意数据）：
- `ModulePlan.memPorts`（来自 Pass3）：
  - `MemoryPortInfo{memory=mem, isRead=true, isWrite=false, isSync=false,`
    `sites=[{location=mem[addr], sequence=3}]}`  // 组合读
  - `MemoryPortInfo{memory=mem, isRead=false, isWrite=true, isSync=true,`
    `sites=[{location=mem[addr] <= wdata, sequence=7}]}`  // 顺序写
- `LoweringPlan.values`：
  - `v0 = Symbol(mem)`（目标符号，仅用于绑定）
  - `v1 = Symbol(addr)`
  - `v2 = Symbol(wdata)`
  - `v3 = Symbol(en)`
  - `v4 = Symbol(clk)`
- `LoweringPlan.loweredStmts`（Write 语句节选）：
  - `LoweredStmt{kind=Write, eventEdges=[posedge], eventOperands=[v4],`
    `write.target=mem, write.slices=[BitSelect(index=v1)], write.value=v2,`
    `write.guard=v3, write.domain=seq}`

输出（示意）：
- `LoweringPlan.memoryReads`：
  - `MemoryReadPort{memory=mem, address=v1, data=<mem[addr] node>, isSync=false}`
- `LoweringPlan.memoryWrites`：
  - `MemoryWritePort{memory=mem, address=v1, data=v2, mask=all1, updateCond=v3,`
    `eventEdges=[posedge], eventOperands=[v4]}`
补充说明：
- `isSync` 由 Pass3 的 `ControlDomain` 决定（seq -> true，comb -> false）；
- `sites` 保留访问次序与位置，用于端口数推断；Pass7 仅消费其合并后的端口形态。

## Pass8: GraphAssemblyPass
- 功能：把计划与中间产物组装为 GRH Graph，生成端口、Value 与 op。
- 输入：`ModulePlan` + `LoweringPlan` + `WriteBackPlan` + `Netlist` + ctx。
- 输出形式：`Netlist` 中新增 `Graph` 与 topGraphs 标记。

### 形式化描述
- 记号：
  - `P` 为 `ModulePlan`，`L` 为 `LoweringPlan`，`W` 为 `WriteBackPlan`。
  - `S(x)` 表示 `PlanSymbolId -> GraphSymbolId` 的映射。
  - `V(x)` 表示 `PlanSymbolId -> ValueId` 的映射。
  - `E(i)` 表示 `ExprNodeId -> ValueId` 的映射。
- 预条件：
  - `P.ports` 与 `P.signals` 已完成宽度/签名解析；
  - `L.values` 仅包含 Pass4/5 支持的节点；
  - `W.entries` 已包含切片写回的 read/modify/write 结果。
- 目标：
  - 生成 `Graph G`，包含端口 Value、内部 Value、运算 op，并写入 `Netlist`；
  - 保持 SSA：每个 Value 只由一个 op 定义。
- 规则：
  1. **Graph 命名**：`G.symbol = P.moduleSymbol`，冲突时追加后缀。
  2. **端口**：
     - input/output：为端口创建 Value，绑定到 Graph ports；
     - inout：创建 `__in/__out/__oe` 三 Value 并绑定 inout port。
  3. **信号**：
     - 非 memory 信号创建 Value；
     - memory 信号创建 `kMemory` op，写入 width/row/isSigned。
  4. **Memory 端口**：
     - memoryReads：`kMemoryReadPort`；顺序读（`isSync`）追加 `kRegister`，
       使用 `updateCond/eventEdges/eventOperands`；
     - memoryWrites：`kMemoryWritePort`，写入 updateCond/addr/data/mask/eventEdge；
     - 缺失 edge-sensitive 事件时发出诊断并跳过。
  5. **副作用语句与 DPI**：
     - `Display` -> `kDisplay`：operands = `updateCond + args + eventOperands`；
       attrs = `formatString/displayKind/eventEdge`。
     - `Assert` -> `kAssert`：operands = `updateCond + condition + eventOperands`；
       attrs = `message/severity/eventEdge`。
     - `DpiCall` -> `kDpicCall`：operands = `updateCond + inArgs + eventOperands`，
       results = `[ret?] + outArgs`；attrs = `targetImportSymbol/inArgName/outArgName/hasReturn/eventEdge`。
     - `dpiImports` -> `kDpicImport`：attrs = `argsName/argsDirection/argsWidth/argsSigned`
       与返回值 `hasReturn/returnWidth/returnSigned`。
  6. **实例化**：
     - 普通实例：`kInstance`，attrs = `moduleName/inputPortName/outputPortName[/inoutPortName]/[instanceName]`。
     - 黑盒实例：`kBlackbox`，attrs 额外包含 `parameterNames/parameterValues`。
     - 输入端口连接：对连接表达式进行一次本地 ExprLowerer（仅支持基础表达式/选择/拼接）。
     - 输出端口连接：必须是简单 symbol（禁止连接 inout 绑定、复杂表达式）。
     - inout 端口连接：要求连接到 inout 端口，按 `__out/__oe` 作为 operands，
       `__in` 作为 result。
     - moduleName：非黑盒通过 `PlanKey.paramSignature` 映射到 Graph 名，黑盒使用模块定义名。
  7. **表达式**：
     - `Constant` -> `kConstant` + `constValue` attr；
     - `Symbol` -> 直接映射 `V(symbol)`；
     - `Operation` -> 对 operands 递归发射，创建同名 GRH op，并生成临时 Value。
     - `kReplicate` 需要常量 count；`kSliceDynamic` 使用 `sliceWidth` attr。
  8. **写回**：
     - comb：`kAssign(nextValue)` 输出到目标 Value；
     - seq：`kRegister(updateCond, nextValue, eventOperands...)`，写 `eventEdge`；
     - latch：`kLatch(updateCond, nextValue)`。
  9. **顶层标记**：对顶层 `PlanKey` 调用 `netlist.markAsTop()`，并注册实例名/定义名 alias。

### 案例讲解（基于核心数据结构的转换）
示例输入（SystemVerilog）：
```
module graph_assembly_basic(
    input  logic clk,
    input  logic a,
    input  logic b,
    input  logic en,
    output logic y,
    output logic q,
    output logic l
);
    assign y = a & b;
    always_ff @(posedge clk) if (en) q <= a;
    always_latch if (en) l <= b;
endmodule
```

给定（示意数据）：
- `ModulePlan.ports`：
  - `clk/a/b/en`：input，`width=1`；
  - `y/q/l`：output，`width=1`。
- `LoweringPlan.values`（节选）：
  - `v0 = Symbol(a)`
  - `v1 = Symbol(b)`
  - `v2 = Op(kAnd, [v0, v1])`
  - `v3 = Symbol(en)`
  - `v4 = Symbol(clk)`
- `WriteBackPlan.entries`：
  - `Entry{target=y, domain=comb, updateCond=invalid, nextValue=v2}`
  - `Entry{target=q, domain=seq, updateCond=v3, nextValue=v0, eventEdges=[posedge], eventOperands=[v4]}`
  - `Entry{target=l, domain=latch, updateCond=v3, nextValue=v1}`

转换结果（示意）：
- 端口：从 `ModulePlan.ports` 创建 `clk/a/b/en` 的 input Value 与 `y/q/l` 的 output Value。
- 表达式：从 `LoweringPlan.values` 发射 `kAnd(a,b) -> t0`（对应 `v2`）。
- 写回：
  - comb：`kAssign(t0) -> y`
  - seq：`kRegister(updateCond=en, nextValue=a, eventEdge=[posedge], eventOperands=[clk]) -> q`
  - latch：`kLatch(updateCond=en, nextValue=b) -> l`

## 输出
- 返回 `Netlist` 给 emit/transform 等下游，诊断信息交由 CLI 输出。
