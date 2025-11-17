# 阶段22：$display/$write/$strobe 映射为 kDisplay

## 完成内容
- **KR1（识别入口）**：在 `AlwaysConverter::visitExpressionStatement` 中捕获 `CallExpression`，若为系统任务且名称匹配 `$display/$write/$strobe`，则交由各自的 always converter 处理。顺序/组合流程共用一套检测逻辑，避免遗漏 `timed` 包装场景。
- **KR2（时序/组合区分）**：
  - `CombAlwaysConverter` 对 display 族任务直接发出 `nyi` 警告，提示“组合流程中忽略调试打印”，并继续 elaboration。
  - `SeqAlwaysConverter` 引入 `handleDisplaySystemTask`：确保存在有效时钟（重用 `ensureClockValue()`）与 guard（空 guard 时生成常量 1），仅对具备时序语义的过程块生成 kDisplay。
- **KR3（构建 kDisplay）**：
  - 记录并缓存 `deriveClockValue()` 推导出的 `clkPolarity`，作为 display 属性；
  - 格式串策略：若首参为字符串字面量，直接使用其值；否则为每个参数生成 `%0d`，保证输出与 SystemVerilog 语义兼容；
  - RHS 解析全部走 `SeqAlwaysRHSConverter`，operand 顺序固定为 `[clk, enable, var0, var1, ...]`；attributes 包含 `clkPolarity`、`formatString`、`displayKind`（display/write/strobe）。
- **KR4（测试样例）**：
  - `tests/data/elaborate/seq_always.sv` 新增 `seq_stage22_display_basic`（基本 display）、`seq_stage22_guarded_write`（guard 控制 write）、`seq_stage22_strobe`（strobe 变体）、`comb_stage22_display_warning`（组合 warning）。
  - `tests/elaborate/test_elaborate_seq_always.cpp` 增补断言：检查 kDisplay 操作数绑定、格式串、displayKind、guard 使能来源、组合 warning 过滤等。
- 文档同步：在 `docs/GRH-representation.md` 的 `kDisplay` 条目补充 `displayKind` 属性描述，以便后续阶段引用。

## 实现要点
1. **系统任务检测与复用**：通过 `CallExpression::getSubroutineName()` 粗粒度匹配，再由 `normalizeDisplayKind` 统一名称；组合/顺序共用 `handleSystemCall` 入口，避免重复 AST 遍历。
2. **Guard 与 enable**：当前 guard stack 提供 1-bit 条件，顺序 display 的 enable operand 来自 `coerceToCondition(currentGuard)`，无 guard 时调用 `createOneValue(1)` 复用常量缓存。
3. **格式串处理**：为保持实现简单，阶段22仅接受字符串字面量格式；遇到字符串表达式/参数化格式时以诊断拒绝。非字面量场景将各参数映射到 `%0d`，满足 Stage22 KR 要求。
4. **Attribute 组合**：`clkPolarity` 与 `displayKind` 以 string attribute 形式下放至 GRH，可供后端决定 `$write` 是否追加换行等差异。
5. **诊断过滤**：seq 测试中新增对白名单 warning 的过滤（组合 display warning），避免将预期告警视为失败。

## 测试记录
- 构建：`cmake --build build`
- 全量测试：`cd build && ctest --output-on-failure`
- 所有 10 个测试目标（含 `elaborate-seq-always` 与新增 Stage22 样例）均通过。

## 后续建议
1. **格式串进阶**：后续可结合 `FmtHelpers::formatDisplay` 或 SV constant-eval，将 `$display` 参数重写为统一格式串，并支持 `%m/%l` 等特殊占位符。
2. **组合 display 可选支持**：若将来需要保留组合流程下的打印，可在 GRH 中新增 “comb display” 原语或延迟绑定到 Seq pass 中。
3. **多字符串/动态参数诊断**：目前遇到非常规格式（如字符串数组）直接报错，可考虑发出 warning 并自动降级为 `%0s`。
4. **Artifacts 支持**：在未来阶段可将 display 节点导出到 JSON/调试 artifact，配合仿真或等效测试验证打印顺序。
