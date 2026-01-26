# 阶段25：blackbox 支持

## 完成内容
- **KR1（识别 blackbox 模块）** 在 Elaborate 中新增 `BlackboxMemoEntry` 缓存：通过 `(* blackbox *)` 等属性识别黑盒，并以“无内部实现的模块体”为兜底判定；记录端口方向/位宽/符号、有无显式属性/实现等元数据。对既标记 blackbox 又含实现的模块发出诊断后按普通模块处理，避免误降级。
- **KR2（实例化生成 kBlackbox）** `processInstance` 在命中 blackbox memo 时跳过内部建图，直接生成 `kBlackbox` Operation：填充 `moduleName/instanceName/inputPortName/outputPortName/parameterNames/parameterValues` 属性；端口连接复用 TypeHelper 展平位宽并校验，inputs 作为 operands、outputs 作为 results 与父图的 net/reg memo 连线；参数覆盖按实例 `#(...)` 常量折叠写入属性，支持 generate 场景。
- **KR3（测试验证）** 新增 `tests/data/elaborate/blackbox.sv`（含参数化 blackbox 与 generate 实例）和 `tests/elaborate/test_elaborate_blackbox.cpp`，校验生成的两个 `kBlackbox` 节点的端口/参数属性与连线正确，确保 leaf graph 不再含 placeholder。将新用例挂入 CMake/ctest。

## 额外修复
- 修正 DPI 测试中 packed/unpacked 结构导致位宽未知的问题：让 `vec2_t` 完全 packed，并在 `TypeHelper` 标记位宽可知后再参与 DPI 参数校验；`elaborate-dpic` 回归通过。

## 测试记录
- `cmake --build build --target elaborate-blackbox`
- `cd build && ctest -R elaborate-blackbox --output-on-failure`
- `cmake --build build --target elaborate-dpic`
- `cd build && ctest -R dpic --output-on-failure`

## 后续建议
1. 补充对 inout/interface 端口 blackbox 的诊断/支持；当前遇到直接 NYI。
2. 参数值属性现以字符串承载，可按需要区分原始文字与规约值，或在 netlist 导出时保留常量宽度/符号信息。
3. 黑盒判定仍以“无实现”兜底，可考虑与用户配置/库标记联动，避免误将空模块视为黑盒。 
