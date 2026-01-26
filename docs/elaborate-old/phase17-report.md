# 阶段17：SeqAlwaysConverter 完成寄存器 NBA 绑定

## 完成内容
- **kRegister/kRegisterRst/kRegisterArst 生成（KR1）**：`ensureRegState` 会分析时序块的敏感表与首个 `if` 语句，自动判断异步/同步复位，建立对应的 `kRegister*` operation 并写入 `SignalMemoEntry`。Finalize 阶段基于 write-back memo 将 `clk/rst/d` 三类 operand 一次性连接完毕。
- **属性与状态校验（KR2）**：Finalize 过程中检查寄存器 state op 是否尚未被其他块占用、`rstPolarity` / 输出位宽是否与 memo 匹配，如有冲突立即通过 `reportFinalizeIssue` 抛出诊断，避免静默连线。
- **分段合成与覆盖规则（KR3/KR4）**：`buildDataOperand` 会对切片排序、合并并生成 `kConcat`，同一寄存器的重叠写入按照源语句顺序覆盖，高位拼接顺序与 flatten 结果一致。
- **保留未写片段（KR5）**：未赋值的 bit 段通过 `createHoldSlice` 从寄存器 `q` 端截取并拼回，保证数据输入宽度完整且保留上一拍状态。
- **复位路径落地**：`buildResetContext` + `extractResetBranches` 能识别 mux 结构中的 reset 分支，提取零值或用户指定 reset literal，分别接入同步/异步寄存器的 `rst`、`resetValue` operand，真正实现 KR1 中“正确设置 clk/rst/d”的目标。
- **测试与可视化**：`tests/data/elaborate/seq_always.sv` 扩充了普通寄存器、分段赋值、同步复位、异步复位四类 always_ff；`tests/elaborate/test_elaborate_seq_always.cpp` 校验 op kind、operand 顺序、复位 mux、slice/concat 结构以及 `rstPolarity` 属性，`ctest -R elaborate-seq-always` 作为阶段回归。

## 核心实现要点
- **WriteBackMemo 改造**：`Entry` 新增 `consumed` 标记与 `entriesMutable()`，Seq finalize 消费过的写集合不会在通用 write-back 时再触发二次写入。
- **数据拼接流程**：Finalize 对每个寄存器独立排序切片、插入 hold 段并构建局部 `kConcat`，避免 generic concat 在多片段/覆盖场景下丢失位宽信息。
- **时钟与复位求值**：`collectSignalEvents` 负责钟沿提取；同步复位通过遍历 `StatementKind::Timed/Block/List` 找到首个 `ConditionalStatement`，异步复位则从 sensitive list 中解析第二个 event，二者均回填到 memo 中，供 finalize 与 op 生成共享。
- **LHS 覆盖 reg memo**：`LHSConverter` 持有 net+reg memo，SeqAlways 的 LHS 可以直接落到寄存器条目，从而让 write-back memo 记录完整的 NBA 写入信息。

## 测试记录
- `cmake --build build`
- `cd build && ctest --output-on-failure`
  - 重点关注 `elaborate-seq-always`，其 JSON artifact 也可用于人工审查 `seq_always.json`。

## 后续建议
1. **复位表达式扩展**：当前仅支持简单的单变量/取反条件和零常量，可继续覆盖结构体 reset literal、嵌套 if/else 与多级条件，完善 `extractResetBranches` 的模式匹配。
2. **多时钟/复位冲突诊断**：在 memo 分析阶段记录驱动块来源，若检测到同一寄存器被不同时钟或不同复位策略驱动，可以提供更详细的冲突信息（源位置、block 名称）。
3. **复位值可视化**：测试与 artifact 中加入 reset 逻辑的 JSON dump（例如标出 mux 结构、rstPolarity、resetValue Literal），方便后续调试更复杂的 reset 场景。
