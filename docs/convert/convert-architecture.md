# Convert 架构（静态视角）

> 本文描述 Convert 的静态结构：有哪些部件、它们如何组织与协作。

## 目标与边界
- 输入：slang AST（`slang::ast::*`），由前端构建的 `Compilation` 与 `RootSymbol` 提供。
- 输出：GRH IR（`grh::ir::Netlist` / `Graph` / `Value` / `Operation`）。
- 职责：把已解析并类型化的 AST 转为结构化网表，并输出可定位的诊断信息。
- 非职责：覆盖所有 SV 特性、完整时序/综合语义证明。

## 设计原则
- 分阶段：先收集与分析，再生成 IR，避免边扫边写导致状态纠缠。
- 以 slang 类型系统为源：宽度、符号、packed/unpacked 维度、参数值均从 `slang::ast::Type` 与常量求值获取。
- 显式的数据模型：使用 Plan/Info 结构保存信号、端口、读写关系与控制域，降低隐式 memo。
- SSA 与四态：GRH 以 SSA 表达，四态逻辑语义保持从 slang 到 GRH 一致。
- 模块粒度隔离：每个 `InstanceBodySymbol` 形成独立的 ModulePlan 与 Graph，顶层与子实例一致处理。
- 层次保留：不做全局扁平化，实例关系通过 kInstance 维护。
- 可控日志：提供统一的调试日志接口，默认静默，可按级别与模块启用。

## 核心组件
- **ConvertDriver**
  - Convert 的入口与总控，管理 `Netlist`、`ConvertDiagnostics`、选项与全局缓存。
- **ModulePlanner**
  - 针对一个 `InstanceBodySymbol` 生成 `ModulePlan`，包含端口、信号、读写关系与实例信息。
- **SymbolCollector**
  - 解析 `PortSymbol`/`NetSymbol`/`VariableSymbol`/`InstanceSymbol` 等，形成 `SignalInfo` 与 `PortInfo`。
- **TypeResolver**
  - 统一计算位宽、符号属性、packed/unpacked 维度与 memory 行数。
- **RWAnalyzer**
  - 从连续赋值与过程块提取读写关系，识别复杂 always 类型与可综合控制流（if/for/case），标注控制域（comb/seq/latch）及 clk/rst 语义。
- **Lowering 层**
  - `ExprLowerer`：将 `slang::ast::Expression` 转为 GRH Value 与组合逻辑 Op。
  - `StmtLowerer`：将过程块与控制流转为写回计划与 guard/mux 结构。
  - `MemoryPortLowerer`：将 memory 读写关系映射到 kMemory* 端口 Op。
- **WriteBackAssembler**
  - 汇总同一信号的多路写入，生成 kAssign/kRegister/kLatch 等最终写回节点。
- **GraphAssembler**
  - 根据 ModulePlan 构建 `Graph`，绑定端口、创建实例化 Op，并注册别名。
  - 对非黑盒实例消除参数化，每个参数配置生成独立 Graph。
  - 保留层次结构，不在此阶段做扁平化。
- **ConvertLogger**
  - 统一的调试日志接口，支持 level/tag 过滤，受 options 控制。

## 代码骨架落位
- `include/convert.hpp`：Convert 核心数据结构与接口声明
- `src/convert.cpp`：Convert 骨架实现与默认行为
- `CMakeLists.txt`：新增 `convert` 静态库目标

## 关键数据模型
- **ModulePlan**（模块级静态计划）
  - `ports[]`：端口名、方向、位宽、inout 拆分信息。
  - `signals[]`：信号种类（net/reg/mem）、位宽、签名、packed/unpacked 维度、源 AST。
  - `rwOps[]`：读写事件（LHS/RHS、控制域、clock/reset、优先级）。
  - `memPorts[]`：memory 端口描述（读/写、同步/异步、掩码）。
  - `instances[]`：子实例与端口连接描述。
- **GlobalCache**
  - `graphByBody`：参数特化后的 `InstanceBodySymbol` -> `GraphId`。
  - `symbolIdCache`：GRH 符号驻留与复用。
- **Diagnostics**
  - 统一记录 `todo/error/warn`，带 AST 位置信息与上下文。

## 静态关系（概览）
- `ConvertDriver` 协调 `ModulePlanner` 构建 `ModulePlan`，再由 `GraphAssembler` 生成 `Graph`。
- `ModulePlan` 由 `SymbolCollector + TypeResolver + RWAnalyzer` 共同填充。
- `Lowering` 与 `WriteBackAssembler` 只消耗 Plan，不在分析阶段产生 IR。

## GRH 输出契约
- 端口与信号宽度、符号属性完全基于 slang 类型系统。
- memory 仅使用精简后的读写端口集合（`kMemoryAsyncReadPort` / `kMemorySyncReadPort` / `kMemorySyncReadPortRst` / `kMemorySyncReadPortArst` / `kMemoryWritePort` / `kMemoryMaskWritePort`）。
- 读端口复位仅作用于读寄存器，写端口无复位语义。
- 四态逻辑语义保持一致，常量允许 x/z。
- Netlist 允许多个顶层模块，Convert 标记 `topInstances` 形成 topGraphs 列表。
