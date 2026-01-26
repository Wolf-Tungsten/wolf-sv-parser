# Elaborate 架构（静态视角）

> 本文提供当前 Elaborate 的粗线条结构概览，聚焦“有哪些部件、它们如何组织”。细节留待后续补充。

## 目标与边界
- **输入**：slang AST（`slang::ast::*`），由 CLI/front-end 构建的 `Compilation` 提供。
- **输出**：GRH IR（`grh::ir::Netlist` / `Graph` / `Value` / `Operation`）。
- **职责**：把语义化 AST 转成可落地的结构化 netlist，并在过程中生成可定位的诊断信息。
- **非职责（暂不覆盖）**：所有 SV 特性全量支持、组合/时序细节的完整语义证明。

## 核心组件
- **Elaborate 入口类**（`include/elaborate.hpp`, `src/elaborate.cpp`）
  - 统一管理转换流程：图构建、端口/信号 memo、assign/always/实例等降级。
  - 通过 `ElaborateOptions` 控制阶段性行为（例如 placeholder）。
- **诊断体系**（`ElaborateDiagnostics`）
  - 输出 `Todo/Error/Warning` 三类诊断。
  - 与 AST 源位置关联，便于 CLI 打印。
- **Memo 体系**（见 `docs/elaborate/memo.md`）
  - `netMemo_ / regMemo_ / memMemo_`：记录信号符号、位宽、flatten 字段、驱动信息。
  - `writeBackMemo_`：记录 LHS 写意图，延迟到块/模块结束统一写回。
  - `dpiImports_`：记录 DPI import 元数据与生成的 GRH 节点。
  - `inoutMemo_`：记录 InOut 端口的 in/out/oe 映射。
  - `blackboxMemo_`：记录 blackbox 模块端口/参数与状态。
- **转换器（Converters）**
  - `CombRHSConverter`：组合 RHS 表达式降级为 GRH Value。
  - `LHSConverter` 派生类：连续赋值/always_comb 等 LHS 解析。
  - `CombAlwaysConverter` / `SeqAlwaysConverter`：过程块控制流与写回逻辑。
- **IR 组织**
  - `grh::ir::Netlist` 管理多个 `Graph`（模块/实例体）。
  - `Graph` 内部包含 `Value` 与 `Operation`（如 kInstance/kAssign/kRegister 等）。
  - **内存读语义**：时序块里的 memory read 以 `kMemorySyncReadPort` 表示，但 SV emit 当前按组合读（`assign mem[addr]`）输出，不引入额外寄存器延迟；使“同周期可见”的语义由外围控制逻辑（mux/guard）保证。

## 主要数据关系
- **Graph 与 InstanceBody**：`graphByBody_` 维护 canonical body → Graph 的映射，用于复用；不同参数配置通常对应不同的 body，因此会产生独立 graph。
- **端口绑定**：端口方向映射到 `Graph` 的 input/output/inout 绑定，内部 value 与 memo 关联。
- **写回模型**：所有赋值先落入 `WriteBackMemo`，最终统一写回到 net/reg 的 SSA 结构。
- **调试信息**：`SourceManager` 将 AST 位置信息映射到 GRH 节点，便于输出。

## 依赖与协作
- **上游**：slang front-end/CLI 负责解析与 `RootSymbol` 构建。
- **下游**：emit/grh 子系统消费 `Netlist` 生成 JSON/SV 等输出。
- **测试**：`tests/elaborate/*` 使用 driver 走通完整流程并验证 GRH 结构。

## 可扩展点（粗线条）
- 新增 AST 语法支持（更多表达式、过程块、generate 变体）。
- 增强 memo 结构以支撑更复杂的时序/存储语义。
- 更完整的诊断覆盖与错误恢复策略。

## Elaborate 入口类展开（当前视角）
- **职责边界**：负责从 `RootSymbol` 拉起全流程，统一协调 graph 构建、memo 生命周期、诊断输出与子模块递归。
- **核心入口**：`Elaborate::convert` 驱动顶层实例遍历与 netlist 构建；`convertInstanceBody` 承担模块体展开。
- **图管理**：`materializeGraph` 以 canonical body 为 key 复用 graph；同一参数配置的实例只构建一次，不同参数配置会生成不同 graph，并注册别名供 emit 使用。
- **端口模型**：`populatePorts` 生成端口 value 并绑定到 graph；InOut 端口拆成 `in/out/oe` 三路并记录 memo。
- **memo 生命周期**：`collectSignalMemos`/`materializeSignalMemos` 形成 net/reg/mem 的全局锚点；`ensureWriteBackMemo` 与 `finalizeWriteBackMemo` 封装写回收敛。
- **过程块与赋值**：连续赋值与 always 块被统一路由到 converter，最终写回到 memo（而非直接改写 value）。
- **黑盒与 DPI**：`ensureBlackboxMemo` 决定是否展开内部；`collectDpiImports`/`materializeDpiImports` 在模块图上注册 DPI 入口节点。
- **诊断策略**：暂不支持/灰度特性通过 `ElaborateDiagnostics` 输出；`Error` 会终止 elaboration，`Todo/Warning` 继续执行。

## 诊断体系细化（异常与接口）
- **是否抛异常**：`ElaborateDiagnostics::error` 只记录错误；是否终止由流程控制决定（默认开启 `abortOnError` 时会抛出 `ElaborateAbort`）。
- **标准接口**：`ElaborateDiagnostics` 提供统一入口，`todo/error/warn` 分别对应 TODO/Error/Warn 级别；既支持 `Symbol`（自动生成 `originSymbol` 与 `location`），也支持直接传 `SourceLocation` 的接口。
- **数据结构**：`ElaborateDiagnostic` 记录 `kind/message/originSymbol/location`，保证 CLI 能打印符号路径与源码位置。
- **退出策略（CLI）**：CLI 会把 `Error` 视为错误，打印诊断后返回非零退出码；`Todo/Warning` 仅提示不阻断。
- **调用约定**：Elaborate 构造可传空指针诊断器，此时不会收集信息；正常流程建议统一通过 `diagnostics_` 上报。是否终止由 `ElaborateOptions::abortOnError` 控制。
- **测试用法**：需要继续生成中间结果时，可将 `abortOnError=false` 以保留诊断并不中断流程。

**代码片段示例：在 Elaborate 内部上报 Error（默认会终止）**
```cpp
if (diagnostics_) {
    diagnostics_->error(*block, "Procedural block kind is not supported yet");
}
```

**代码片段示例：CLI 侧把 Error 视为错误并终止**
```cpp
grh::ir::Netlist netlist;
try {
    netlist = elaborator.convert(root);
} catch (const wolf_sv_parser::ElaborateAbort&) {
    // Diagnostics already recorded; stop elaboration immediately.
}
bool hasElaborateError = false;
for (const auto& message : elaborateDiagnostics.messages()) {
    if (message.kind == wolf_sv_parser::ElaborateDiagnosticKind::Error) {
        hasElaborateError = true;
    }
}
if (hasElaborateError) {
    return 2;
}
```
