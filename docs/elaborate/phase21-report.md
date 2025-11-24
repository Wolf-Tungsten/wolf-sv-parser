# 阶段21：使能寄存器原语（kRegisterEn/EnArst）落地

## 完成内容
- KR1（寄存器使能识别）：在顺序 always 路径中识别典型保持语义产生的使能结构，将
  - `mux(en, d, Q)` 折叠为 `kRegisterEn`（操作数顺序：[clk, en, d]）；
  - `mux(rst?rv:d, ...)` 的复位已在阶段17提取，复位+保持下的 `mux(en, d, Q)` 进一步特化为 `kRegisterEnRst/kRegisterEnArst`（操作数顺序：[clk, rst, en, resetValue, d]）。
- KR2（复位与使能协同）：保持阶段17的复位提取（从 data 路 `kMux` 中抽取 rst/rv），在此基础上“剥离”复位后的 data，再判断是否存在 `mux(en, d, Q)` 以提取使能。
- KR3（操作数顺序与宽度一致性）：
  - `kRegisterEn` 在连接 data 之前具备 `[clk, en]` 两个操作数；
  - `kRegisterEnRst/kRegisterEnArst` 在连接 data 之前具备 `[clk, rst, en, resetValue]` 四个操作数；
  - `attachDataOperand` 在不同原语下校验操作数数目与 data/Q 宽度一致后再接入 data。
- KR4（测试用例）：`tests/elaborate/test_elaborate_stage21.cpp` 覆盖
  - `seq_stage21_en_reg` => 期望 `kRegisterEn([clk,en,d])`；
  - `seq_stage21_rst_en_reg` => 期望 `kRegisterEnArst([clk,rst_n,en,rv,d])`。
  结合阶段17/18/19/20 的既有断言，`ctest -R elaborate-stage21` 与全量回归均通过。

## 核心实现要点
- 识别入口：`SeqAlwaysConverter::finalizeRegisterWrites`
  1) 先通过 `buildDataOperand(entry)` 将最终要写入寄存器的 RHS 组合完成（拼接 hold 片段与各 slice）。
  2) 复位提取：`buildResetContext` + `extractResetBranches` 从 data 路 `kMux` 中解析 `rst`、`resetValue` 与 “去除复位”的 data。
  3) 使能提取：在“剥离复位”后的 data 上定位 `kMux(cond, tVal, fVal)`，若 `fVal == Q`（或 `tVal == Q`）则判定为保持语义；
     - 将条件规约为 1-bit（`coerceToCondition`），若 `tVal == Q` 则对条件取反；
     - 按当前原语类型重写：
       - `kRegister` => `kRegisterEn` 并插入 `en`；
       - `kRegisterRst` => `kRegisterEnRst`，重排操作数为 `[clk, rst, en, resetValue]`；
       - `kRegisterArst` => `kRegisterEnArst`，重排操作数为 `[clk, rst, en, resetValue]`；
     - 使用“去除复位/保持”的 `newData` 作为最终 data。
  4) 最后由 `attachDataOperand` 接上 data，并做宽度一致性校验。
- 复位/使能与保持的关系：
  - 对于保持（缺分支写入），顺序语义下 `mergeShadowFrames` 会自动补齐缺失分支为 `Q`，形成 `mux(en, new_data, Q)`；
  - 对已含复位的场景，先抽取复位再判断 `en`，确保最终原语与操作数序一致。
- 宽度与顺序约束：
  - `attachClockOperand/attachResetOperands/attachDataOperand` 分别确保 clock 先于 reset，再先于 enable（对 EnRst/EnArst 在 data 前应已有 `[clk,rst,en,rv]`）。
  - 数据宽度必须与 Q 相等，否则报 NYI 诊断并放弃 finalize（保证图一致性）。

## 与前序阶段的协作
- 阶段17（复位寄存器）：复位提取机制沿用，并作为使能折叠的前置步骤。
- 阶段19（if/case 合流）：保持语义由分支合流与 hold 片段共同构成，是使能折叠的直接来源。
- 阶段20（循环）：循环体内多次写回的“最后一次写覆盖”在 `buildDataOperand` 阶段线性化后，再进行复位/使能分析，不影响判定正确性。

## 测试记录
- 构建：`cmake --build build`
- 运行：
  - `cd build && ctest -R elaborate-stage21 --output-on-failure`
  - 全量：`cd build && ctest --output-on-failure`
- 结果：阶段21专用用例与全量用例全部通过。

## 影响评估
- 语义更贴近目标 IR：将常见 `if (en)` 结构直接抽象为原语，有利于后续优化与后端映射。
- 对于无法识别为标准保持/使能模式的复杂数据路，仍保留 `kMux` 结构（保持回退策略），不影响正确性。
- 与 memory 行为（阶段18/19）互不干扰；时序 always 的端口 `enable` 仍按 guard 传递。

## 后续建议
1. 更强的使能模式识别：支持多层嵌套/归一化后的等价结构（例如经过布尔代数简化、取反后的等价使能）。
2. 属性增强：为使能/复位在原语上附加来源信息（例如源表达式位置、静态/动态来源），帮助调试与后端优化。
3. 结构匹配扩展：识别“数据路径恒等传递”（assign/concat 的平凡组合）并收敛为更简洁的输入连接。
4. 与组合/时序混合复位场景的边界处理：明确诊断并提供改写建议，保证最终 IR 易于综合与映射。
