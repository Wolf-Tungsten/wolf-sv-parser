# CombAlwaysConverter 控制流笔记

## 参考资料
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

本文档可配合 `docs/elaborate/lhs-converter.md` 与阶段 13 的代码阅读，定位 `if`/`case` 在 shadow/write-back 之间的具体数据流。EOF
