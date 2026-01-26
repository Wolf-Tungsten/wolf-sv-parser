# 阶段23：断言映射为 kAssert

## 完成内容
- **KR1（入口识别）** 在 `AlwaysConverter::visitImmediateAssertion` 处理中捕获 `ImmediateAssertionStatement`，仅支持 `AssertionKind::Assert`，忽略 deferred/final 断言；在 `handleSystemCall` 中识别 `$fatal/$error/$warning` 系统任务并归类为断言 intent，同时提取字符串字面量作为 message。
- **KR2（流程限定）** Comb always 遇到断言 intent 发出 warning（“组合 always 中的 assert 被忽略；GRH 仅建模时序断言”）并跳过；Seq always 仅在存在有效 edge 时钟时生成 kAssert，缺钟时报错。
- **KR3（kAssert 构建）** Seq 路径下将断言条件通过 `SeqAlwaysRHSConverter` 降为 GRH Value，结合当前 guard 生成 `guard -> cond`（`!guard || cond`）的 1-bit 条件；节点 operands 为 `[clk, condition]`，attribute 填充 `clkPolarity`，若有 message/severity 也写入。顺序插入，保持语句序语义。
- **KR4（测试）** `tests/data/elaborate/seq_always.sv` 新增 `seq_stage23_assert_basic / seq_stage23_assert_guard / comb_stage23_assert_warning`；`tests/elaborate/test_elaborate_seq_always.cpp` 校验 kAssert 生成、clk/condition 绑定、message 传递与组合 warning；全量 `ctest --output-on-failure` 通过。

## 实现要点
1. **SystemTask 归一**：添加 `classifyAssertSystemTask` 将 `$fatal/$error/$warning` 降为 severity；`tryExtractMessageLiteral` 从首参字符串字面量提取 message。
2. **Guard 融合**：使用已有 guard stack 和 `coerceToCondition` 将断言条件与 guard 做蕴含，保证断言生效域与原始控制流一致。
3. **时钟上下文复用**：seq 断言复用 `ensureClockValue` 与 `clkPolarity` 缓存（阶段22 复用），确保属性完整；无有效时钟时诊断。
4. **属性扩展**：在 `docs/GRH-representation.md` 中为 kAssert 增补可选 `message`、`severity` 说明。

## 测试记录
- 构建：`cmake --build build`
- 运行：`cd build && ctest --output-on-failure`
- 结果：全部 10 个测试目标通过，其中 `elaborate-seq-always` 覆盖 Stage23 新样例。

## 后续建议
1. 支持 `assert property` / `cover` / `assume` 等并发断言，处理 disable iff 与 clocking block，更贴近 SV 语义。
2. message/severity 目前仅支持字符串字面量 + 三档级别，可考虑兼容参数化字符串与 `$display` 风格格式化。
3. 对 deferred/final 立即断言、#delay 断言添加更细粒度诊断或可选降级策略。
