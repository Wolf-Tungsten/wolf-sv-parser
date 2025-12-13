# C910 CASE=003 处理报告

## 背景
- 命令 `make run_c910_test CASE=003` 在 elaboration 阶段报错：`Unsupported RHS call expression (kind=21 Call)`，定位到 `ct_fadd_close_s1_d.v:102` 的 `$unsigned($signed(...))` 组合。
- 失败原因：RHSConverter 仅支持可静态求值的调用，未实现对 `$signed/$unsigned` 系统函数的处理，触发 NYI 终止。

## 修复方案
- 在 `src/elaborate.cpp` 的 `RHSConverter::convertCall` 中，识别系统调用 `$signed/$unsigned`，对首个实参进行 RHS 转换并按目标类型走现有 `resizeValue` 逻辑，这样符号/宽度转换能落入 GRH 图，不再报 NYI。

## 结果
- 重新构建并运行：`cmake --build build -j$(nproc)`，`make run_c910_test CASE=003`。
- 测试通过，Verilator 仿真完成且覆盖率生成，构建日志显示 `Build succeeded: 0 errors, 0 warnings`。
