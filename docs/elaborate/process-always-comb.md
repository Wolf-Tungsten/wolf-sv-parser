# CombAlwaysConverter 控制流笔记

## 参考资料
- `docs/reference/yosys/docs/source/appendix/primer.rst#L366-L381`：强调综合工具必须在 elaboration 期间完全展开可综合循环与条件，提供我们静态展开的语义依据。
- `docs/reference/yosys/docs/source/yosys_internals/flow/verilog_frontend.rst#L205-L256`：描述 Yosys AST 简化阶段会 inline task/function、评估 generate 语句并 unroll 所有 for 循环，证明「先 const-eval、再展开」的流程可行。
- `docs/reference/yosys/passes/proc/proc_mux.cc` 描述了 Yosys 如何把过程块里的 `if`/`case` 转成 `RTLIL::Proc` 节点，再由 `proc_mux` pass 展平成 `pmux`/`mux` 图。
- `docs/reference/yosys/passes/proc/proc_case.cc` 解释了 `case` 的匹配与优先级算法，明确「先匹配者优先」以及 `default` 如何构成最终的 fallback。
- `docs/reference/yosys/docs/CHAPTER_Proc.tex`（以及同目录下的 `PROC_SYNTH`、`PROC_DLATCH` 章节）记录了 Yosys 为避免 latch 而做的覆盖检查：只要某个信号在控制流分支中缺少驱动，就会被 `proc_dlatch` 捕获并最终生成锁存器，这也是我们要显式拒绝的模式。

## 条件归一化
1. 所有 `if`/`case` 条件先走 RHSConverter，得到一个 `grh::Value`。Yosys 会把表达式规范化为 `RTLIL::SigSpec` 并在需要时追加零扩展，我们在 GRH 中等价地保持原位宽。
2. case item 的匹配通过 `==` / `!=` 构造 1bit 结果；多个表达式使用 OR 折叠，类似 Yosys 在 `proc_case` 里生成 `pmux` select 向量。
3. `unique`/`priority` 信息目前只用于诊断文本，优先级顺序仍与源代码项的出现顺序一致（靠“自底向上折叠”保证“先出现者优先”）。

## if/else 降级流程
1. 以当前 block shadow（writeBack 之前的 SSA 快照）复制出 true/false 两个 ShadowFrame，里面带有 net memo 的完整切片。
2. 分别在 true/false frame 上遍历语句，过程与常规 `always_comb` 相同，阻塞赋值写入 shadow。
3. 计算触达的目标集合 `coverage = touchedTrue ∪ touchedFalse`；若某个目标不在两侧同时出现，则按照 Yosys `proc_dlatch` 的策略判定为 latch 候选，直接报错拒绝继续。
4. 对 coverage 中每个目标：
   - 调用 `rebuildShadowValue` 得到 true/false branch 的完整 Value（缺失位段以 0 补齐，等效于 `pmux` 中的“无驱动”填 0）。
   - 创建 `kMux`，条件为步骤1中得到的布尔值，true/false 分支分别对接上述 Value。
   - 把结果写回父 ShadowFrame，等效于 Yosys 在 `proc_mux` 里把 `pmux` 结果重新赋给左值。
5. 合并完成后，父 ShadowFrame 即表示 `if` 语句执行完的 SSA 状态，后续语句继续在其上原地追加。

## case 降级流程
Yosys 的 `proc_case`/`proc_mux` 组合做法是：
1. 控制表达式 `expr` 先下沉为一个 `SigSpec`，每个 case item 里的表达式和 `expr` 做等值比较，再将所有匹配信号串成立即匹配的选择向量。
2. `default` case 作为最终 fallback，当缺少 `default` 时由 `proc_dlatch` 插入锁存器。
3. case item 以从下到上的顺序折叠成一条 mux chain，保证语句顺序即优先级顺序。

我们在 CombAlwaysConverter 中遵循相同策略：
1. 为 case 语句预先复制一个「入口 ShadowFrame」，每个 item / default 执行时都从该入口快照开始，保证分支之间互不窜写。
2. 若 case 体内缺少 `default`，会把进入 case 前的 ShadowFrame 当作 fallback，让常见的「先写默认，case 覆盖」形式保持可综合；若 fallback 中仍未对目标写入，则继续报 latch。
3. 依次（逆序）将每个 item 的 ShadowFrame 与当前累积结果做一次 `mergeBranches(match, branchFrame, accumulator)`：
   - `match` 由若干 `kEq` + `kOr` 构成的 1bit `grh::Value`；
   - `mergeBranches` 内部逻辑与 if/else 相同，逐目标生成 `kMux` 并写入新的切片。
4. `unique`/`priority` 不会改变 mux 结构，但会被串进诊断信息，方便后续阶段根据需要扩展短路/并行行为。
5. `casex/casez`：当 case item 是常量表达式时，会解析出 wildcard mask（`casex` 忽略常量中的 `x/z`，`casez` 忽略 `z/?`），并生成 `kXor -> kAnd(mask) -> kEq(0)` 结构来实现按位匹配；若表达式不是常量则退化为普通 `case` 语义。

## 防止 latch 的约束
- **if**：必须带 `else`，且 true/false 两侧对所有受影响的目标都有写入；否则报错 `comb always if lacks full coverage for <signal>`。
- **case**：必须带 `default`，并且所有 item（含 default）在触达集合上完全一致；少任一目标都判为不可综合 latch。
- 触达集合仅统计当前结构内新增的写入，不会把 if 前的“默认赋值”算进去，方便用户按「先写默认，再用 if/else 覆盖」的经典形态编码。

## unique/priority 静态检查
- 当 case 语句带有 `unique` / `unique0` 修饰符时，会在合成 mux 之前用 `EvalContext` 折算出所有常量 case item，如果发现存在重复值（意味着两个分支可能同时命中），立即发出 `unique case items overlap on constant value ...` 诊断并终止该语句的转换。
- `priority case` 仍要求显式 `default`，否则沿用 latch 规则报错；后续可以在此基础上扩展“至少命中一分支”的静态验证或运行期 `kAssert` 生成。
- 目前 `unique if` / `priority if` 只复用通用的 coverage/latch 检查；一旦需要更精细的静态分析（如常量条件冲突），可在 `mergeShadowFrames` 入口增加条件求值与冲突检测。

## 阶段13落地要点
1. Shadow memo 改为栈结构，允许在进入 if/case 时创建独立快照；离开控制流后再把结果写回上一层。
2. `mergeBranches` 复用 `WriteBackMemo::Slice` 的扁平路径，合流后统一插入覆盖整个目标位宽的一条切片，保证后续语句可以继续基于 SSA 状态切片。
3. 所有 `kMux`/`kEq`/`kOr` 的命名沿用 `makeShadowOpName` / `makeShadowValueName` 规则，便于测试 JSON artifact 定位。
4. 控制流失败（条件不支持、缺少 default、缺少覆盖）时要立刻发出诊断并停止该语句的转换，避免写回半成品。

## 循环展开（for/foreach）

### 目标范围
1. 仅处理组合过程块内的 `for`、`foreach`，不涉及 `repeat`/`while`/`do-while`（这些应在解析前被拒绝）。
2. 所有循环都在 elaboration 期间展开成静态语句序列；Graph 中不出现循环控制节点，shadow/write-back memo 仍按「顺序执行」模型推进。
3. 当循环参数无法静态求值时立即报错，提示需要常量边界；静态 break/continue 在展开阶段裁剪，无法静态决定的 break/continue 同样报错。

### 核心执行模型
1. **LoopDescriptor**（`ForLoopVarState` / `ForeachDimState`）分别捕获 for 的迭代变量与 foreach 的维度信息（范围、步长、loopVar），并以栈方式挂到 `CombAlwaysConverter`。
2. **prepareLoopDescriptor(ast::ForLoop)**：const-eval 初始化语句和条件表达式；计算最多迭代次数（默认 4096，超限报错）；记录当前 loop depth 方便诊断。
3. **prepareLoopDescriptor(ast::ForeachLoop)**：依赖 `TypeHelper` 提供的字段 / 索引列表，枚举 packed/unpacked 维度并检查 loopVar 与维度匹配。
4. **unrollLoop**：复制入口 ShadowFrame，为每个迭代实例 push 新的 frame，执行 body 后 merge 回父 frame；break/continue 在迭代结束后根据 `pendingLoopControl_` 剪枝。

### for 语义
1. 支持 `for (init; cond; step)`：`init` 需是静态赋值语句（可复用现有局部变量），`cond` 和 `step` 通过 EvalContext 评估成常量。
2. 允许缺少 inline loopVar 的写法，只要 initializer 列表是针对符号的赋值；step 里的赋值会先 eval 右值再写回 EvalContext，为下一轮迭代提供新的常量值。
3. 迭代变量作用域与 SystemVerilog 一致：loop 结束后销毁；实现上在 LoopScopeGuard 中维护 `ValueSymbol -> ConstantValue`，供 RHS/LHS 查询。
4. 允许嵌套 for：LoopContextGuard 追踪 loop depth，并在 break/continue 时检查是否仍处于静态上下文（若非静态则报错）。
5. RHS 读取 loopVar 由 `AlwaysBlockRHSConverter` 直接返回 loop scope 中的 literal，并在命中自定义值时禁用 RHS 缓存，避免跨迭代复用旧值。

### foreach 语义
1. 仅支持静态数组 / 结构体：range 必须是常量，动态数组（或未 flatten 的类型）会直接报 `NYI`。
2. 每个迭代维度若指定 loopVar，则在迭代前把索引常量写入 loop scope；`dummy[i]` 这种匿名维度依旧允许，但不能贴 infinite range。
3. foreach 体内读取 loopVar 会通过 `CombAlwaysRHSConverter::handleCustomNamedValue` 获取对应常量 Value。

### break / continue
1. `handleLoopControlRequest` 仅接受静态上下文（即当前 ShadowFrame 标记为 static）；否则报错提示“需要静态条件”。
2. `LoopControl::Break`：立即标记当前 loop depth 的 pending control，并要求父循环在迭代结束后终止；`continue` 则跳过本轮剩余语句。
3. 嵌套 break/continue 依赖 `loopContextStack_` 深度匹配，提升错误提示的可读性（例如 break 超出最外层 loop 时立即报错）。

### 诊断与限制
- 常量性检查：报错需包含循环类型、原始源位置、失败原因（init/cond/step）；step 不得为 0。
- 迭代上限：默认 4096 次迭代，超过则提示“循环展开超限”避免生成巨大 GRH。
- 不支持语法：`while`、`do-while`、动态数组 foreach、任务调用修改 loopVar 等，会立即报 `NYI`。

### 测试与可视化
- `tests/data/elaborate/comb_always.sv` 中加入 `comb_always_stage15_{for,foreach,break,continue}` 模块，覆盖循环展开、索引绑定以及 break/continue 场景。
- `tests/elaborate/test_elaborate_comb_always.cpp` 在 `ctest -R elaborate-comb-always` 中解析 `build/artifacts/elaborate/comb_always.json`，验证：
  - for 循环折叠为 `kOr` 链且叶子集包含 `data_even/data_odd`；
  - foreach 形成 `kXor` 链并引用 `src0/src1`；
  - break/continue 输出与期望端口一致。
- JSON artifact 便于人工检查 SSA 结果，如需更多可视化，可拓展为输出 per-iteration trace。

本文档可配合 `docs/elaborate/lhs-converter.md` 与阶段 13 的代码阅读，定位控制流及循环在 shadow/write-back 之间的具体数据流。EOF
