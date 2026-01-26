# Elaborate 阶段7 完成报告

## Objective 进展
- `SignalMemoEntry` 现直接承载 GRH 资源：memo entry 在解析阶段绑定 `grh::Value*`、`grh::Operation*` 以及驱动该寄存器的 `ProceduralBlockSymbol*`，成为后续 pass 与测试用的统一跳板。
- `convertInstanceBody` 在收集完 memo 后立即调用 `materializeSignalMemos`：对 net entry 创建 SSA Value，对 reg entry 推导 kRegister/kMemory 占位符，并将 op attribute（如 `clkPolarity`、`width/row/isSigned`）全部写入。
- elaborate-smoke 重新获得阶段性 TODO 诊断，signal-memo 用例扩展为人类可读的 value/operation 验证输出，覆盖 net/reg/memory 三类绑定。

## KR1：net memo 绑定 Value
- `SignalMemoEntry` 增加 `value` 字段；`ensureNetValues` 复用 `ensureValueForSymbol`，保证 memo 和 graph 使用一致的 SSA node（复用端口与内部信号的唯一 symbol）。
- Value 生成仍严格沿用 TypeHelper 计算出的 width/signedness，通过 memo entry 可以在调试输出中核对 entry 宽度与实际 Value 宽度是否一致。

## KR2：reg memo 绑定时序 Operation
- 新增 `drivingBlock`、`stateOp` 字段；`collectSignalMemos` 在分类过程中记录每个 reg 的唯一 `ProceduralBlockSymbol`，并在多个时序块同时驱动时输出冲突诊断。
- `ensureRegState` 根据类型形态生成两类对象：
  - 对标量/packed 结构：创建 Value 并实例化 `kRegister` Operation，占位 operand 尚未连接，仅设置 result 与 `clkPolarity` attribute（由 `deriveClockPolarity` 自 `SignalEventControl` 推导）。
  - 对带固定 unpacked 维度的类型：通过 `deriveMemoryLayout` 计算 word width / row count / 有符号性，生成 `kMemory`，暂不创建端口；entry 仅记录 `stateOp`，无 SSA Value。
- Memory 与 Register 均通过 `makeOperationNameForSymbol` 延续原信号名，保证后续 pass 能从属性中反向查回来源。

## KR3：测试与可视化
- `tests/elaborate/test_elaborate_signal_memo.cpp` 现在会保留整个 `grh::Netlist` 生命周期，确保 memo 持有的指针有效，并打印「net value」「register op」「memory attr」等信息满足“人类可读检查输出”的要求。
- 测试增加断言：net entry 的 `grh::Value` 与 memo width 相符；reg entry 的 `stateOp` 必须是 `kRegister` 且 result 对齐；`latch_target` 验证 `negedge`，其他 reg 验证 `posedge`；`reg_unpacked_bus` 需生成 `kMemory` 且 width/row/isSigned 符合 flatten 结果。
- `elaborate-smoke` 恢复 TODO 诊断（`emitModulePlaceholder` 同时推送消息），继续保证阶段性占位信息不会被后续修改误删。
