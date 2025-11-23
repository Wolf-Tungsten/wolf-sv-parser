# 阶段4 完成报告：EmitSystemVerilog 基本功能

- **完成概览**：实现 Verilog 兼容（除 DPI-C 部分）输出：每个 Netlist 生成 `grh.sv`，每个 Graph 生成一个 module，按端口/声明/实例/DPI/assign/时序块分段；避免 `always @*`，组合逻辑用 `assign`，同钟同复位寄存器/存储写聚合在共享 `always @(posedge ...)`。
- **产出物**：
  - 代码：新增 `EmitSystemVerilog`（include/emit.hpp, src/emit.cpp），处理常量/算术/切片/寄存器/存储器/实例/黑盒/Display/Assert/DPI 导入与调用；端口声明保持 Verilog-2001 语法，输出 reg/wire 区分，DPI 导入保留 SV 语法。
  - CLI：`--emit-sv` 开关，输出路径默认 `grh.sv`。
  - 测试：`tests/emit/test_emit_sv.cpp` 构造多段网表（寄存器、mem 读写、掩码写、实例、DPI），检查无 `always @*`、端口 reg 宣告、DPI 语句存在；尝试 `verilator --lint-only`，若缺工具则跳过。
  - 构建：CMake 注册 emit-sv 测试目标。
- **验证**：`cmake --build build && ctest` 全部通过，emit-sv 用例在本地环境 OK（lint 未强制）。生成样例见 `build/artifacts/emit/grh.sv`。
