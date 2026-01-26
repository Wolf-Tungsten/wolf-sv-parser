# 阶段14：CombAlwaysConverter 静态分支分析

## 完成内容
- **静态 `if` 折叠**：在进入 `visitConditional` 之前调用 `evaluateStaticCondition`，利用 `EvalContext` 直接求解条件表达式。若结果恒真/恒假，直接在对应分支上运行 `runWithShadowFrame` 并跳过 `mergeShadowFrames`，避免生成任何 `kMux`。
- **静态 `case` 剪枝**：`visitCase` 先调用 slang 自带的 `CaseStatement::getKnownBranch`。只要控制表达式及分支可在 elaboration 期决议，就只执行命中分支（或 default snapshot），不再构建 `kMuxChain`。保留了阶段13的 unique/priority 检查逻辑，保证语义一致。
- **Parameter 常量化**：`RHSConverter::convertNamedValue` 在 memo / graph 缓存失配时，会检测 `ParameterSymbol` 并通过 `createConstantValue` 生成 GRH literal。这样静态条件才能读取 `localparam/parameter`，也避免了 “Unsupported RHS named value” 诊断。
- **Graph 别名**：为了解决参数化实例导致的 `$PARAM=VALUE` 后缀，`grh::Netlist` 增加 `graphAliasByName_` 并允许注册别名。`Elaborate::convert` 用顶层实例名注册别名，从而让测试（及上层工具）可以用直观名字获取 graph。
- **测试扩充**：新增 `comb_always_stage14_static_if` 与 `comb_always_stage14_static_case` 用例，覆盖恒真/恒假 if、纯静态 case、静态 case 包裹动态 if。`elaborate-comb-always` 测试用例新增断言：静态输出应直接连到输入（无 `kMux`），混合分支则仍生成 mux 且操作数正确。

## 算法与数据结构细节
- **静态条件判定**：
  - `evaluateStaticCondition` 复用 `CombAlwaysConverter` 私有 `EvalContext`，直接对 AST 条件调用 `expr.eval(ctx)`，若返回 `ConstantValue` 且 `isTrue/isFalse` 其中之一则给出布尔字面量；含 `X/Z` 或非布尔类型时保持 `std::nullopt`，回退到阶段13路径。
  - `visitConditional` 在拿到静态结论时，只克隆一次 `ShadowFrame` 作为 snapshot；true/false 分支的运行与普通路径一致，但结束后直接把结果帧写回当前 stack，避免生成任何 mux。
  - `visitCase` 使用 `CaseStatement::getKnownBranch`，内部再次利用 EvalContext 求值 case 项。若 `isKnown == true` 且 `matchedStmt` 非空，则只对命中分支执行 `runWithShadowFrame`；若没有命中则退回 snapshot，等价于用户写的 default。
- **Parameter 常量化路径**：
  - `materializeParameterValue` 接受 `NamedValueExpression`，判断 `expr.symbol` 是否为 `ParameterSymbol`。如果是，则读取 `param->getValue()`，确认 `ConstantValue.isInteger()` 后调用 `createConstantValue` 生成 GRH literal，并以 parameter 名称作为命名 hint。
  - 若 parameter 无法解析成整数（例如字符串、struct），则仍提示 “Parameter <name> has unsupported value type...” 以提醒测试。
- **Graph alias**：
  - `grh::Netlist` 新增 `graphAliasByName_`，`findGraph` 先查主 map，再查 alias map。
  - `Elaborate::convert` 在每个顶层 graph `markAsTop` 之后，如果实例具名，则 `registerGraphAlias(instance.name, *graph)`。测试中 `fetchGraphByName("comb_always_stage14_static_if")` 现在可以命中别名，而不关心参数后缀。

## 测试记录
- `cd build && ctest -R elaborate-comb-always --output-on-failure`

## 下一步建议
1. **静态折叠溯源诊断**：目前静态剪枝在调试时不可见，可考虑在 `CombAlwaysConverter` 中记录“由于常量条件跳过了分支”的 trace/diagnostic，方便用户确认优化生效。
2. **跨帧常量传播**：静态判定结果目前只在局部使用，未来可把恒定值写入 `ShadowFrame` 中，以便后续语句复用（类似简单的常量传播），减少 `kAssign`/`kConstant` 噪音。
3. **生成 block / loop 支持**：阶段14的 StaticFrame 与 alias 机制已经铺好基础，下一阶段可在 loops、`generate if/case` 等结构中应用同样的静态决策，并把 alias 能力扩展到非顶层实例。
