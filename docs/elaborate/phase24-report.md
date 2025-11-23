# 阶段24：DPI-C 支持

## 完成内容
- **KR1（DPI import 识别）** 在 Elaborate 层新增 `DpiImportEntry` 结构：遍历模块体内的 `SubroutineSymbol`，筛选 `MethodFlags::DPIImport` 且返回 `void` 的 function，拒绝 task/context/pure/inout 情况并记录诊断；借助 `TypeHelper::analyze` 展平参数位宽与符号，生成 `argsDirection/argsWidth/argsName` 属性后在模块 Graph 上创建 `kDpicImport` `Operation` 并缓存 symbol→op 对应关系。
- **KR2（DPI 调用降级）** `AlwaysConverter::visitExpressionStatement` 新增 DPI path：Comb always 直接 warning，Seq always 复用 guard/clock 语境生成 `kDpicCall`，operands `[clk,en,in*]`、results `out*`，并设置 `clkPolarity/inArgName/outArgName/targetImportSymbol` 属性。输出参数借助 `SeqAlwaysLHSConverter::convertExpression` 与 `WriteBackMemo` 写回，即便 LHS 包裹在赋值节点中也能 fallback 到整 signal slice；输入参数走 Seq RHS converter 校验位宽。
- **KR3（测试覆盖）** 新增 `tests/data/elaborate/dpic.sv` 与 `tests/elaborate/test_elaborate_dpic.cpp`：构造 `dpic_stage24` 模块（import 函数、posedge always 调用、输出寄存器），在测试中检查 `kDpicImport` 属性、`kDpicCall` 的时钟/使能/operand wiring、`targetImportSymbol` 链接以及寄存器 data operand 是否由 call result 驱动；同时允许既有 placeholder 诊断存在。

## 实现要点
1. **Memo 扩展**：`Elaborate` 保存 `dpiImports_` map，并在 `collectSignalMemos` 中对包含 DPI 调用的 procedural block 打上 `Reg` driver，使输出参数自然进入 reg memo；`SeqAlwaysConverter` 也能 fallback 到 memo 构造写回 slice。
2. **调用上下文复用**：Seq always 的 DPI 路径沿用阶段22/23 的 `ensureClockValue`/`ensureGuardValue`，禁止无时序上下文或 enable 无法建立的调用，comb 路径继续报 NYI。
3. **属性与诊断**：生成 `kDpicCall` 时校验 import 参数数量/方向，失败时即时诊断并跳过；保留对暂不支持场景（task/context/pure/inout/结构体输出等）的 `nyi` 文案，方便后续扩展。

## 测试记录
- 构建：`cmake --build build`
- 运行：`cd build && ctest -R elaborate-dpic --output-on-failure`
- 结果：`elaborate-dpic` 通过，验证 `kDpicImport` 属性与 `kDpicCall` wiring；其余既有测试集保持绿色（已在全量构建中跑过）。

## 后续建议
1. 支持 DPI task/context/pure 属性、inout 参数以及非 void 返回值，必要时扩展 GRH 原语或在 attribute 中携带额外元信息。
2. 目前 Seq DPI 输出仅支持指向 memoized net/reg，可在 future 阶段为 `struct`/`array` 输出提供更细粒度 slice 写回能力。
3. `kDpicCall` 尚未处理组合 always/initial 块内的调用降级策略，可根据需求引入异步调用原语或在 Elaborate 前端提前报错。
