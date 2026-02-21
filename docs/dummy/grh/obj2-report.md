# GRH Latch 支持阶段完成报告

## 完成事项
- 表示层：新增 `kLatch`/`kLatchArst` 原语，定义 enLevel/rstPolarity 语义与 1-bit 约束，并在 `docs/GRH-representation.md` 记录生成语义。
- 序列化/emit：OperationKind、toString/parse 完整覆盖；emit-sv 生成 `always_latch` 块，校验使能/复位位宽与属性，保持端口 reg 声明。
- elaborate：`always_latch` 进入 comb 管线；组合覆盖缺失时不再终止，而是以 hold 值生成 mux 并报 latch 警告；write-back 阶段识别 mux-with-self 结构，落地 kLatch/kLatchArst。
- 测试：更新 comb_always 案例期望，将 latch 警告视为可接受诊断；`ctest` 全通过。

## 诊断与兼容
- latch 推断会 emit `NYI` 警告以提醒设计者，但生成 GRH 以便后续流程使用。
- 原有寄存器/内存路径保持不变，`always_comb` 正常覆盖仍无警告。

## 风险与后续
- 仍缺乏针对 latch emit 输出的专门回归（JSON/Verilog 快照）；建议补充小型 kLatch/kLatchArst 样例。
- elaborate 侧的复位提取仅覆盖异步复位 latch，不含同步复位/使能组合；若后续需要扩展，可在 mux 模式匹配中加同步路径。***
