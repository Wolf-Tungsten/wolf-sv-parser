# Elaborate Workflow（动态视角）

> 本文描述当前 Elaborate 的粗线条运行流程，按“从入口到模块体”的时间顺序展开。

## 顶层入口：`Elaborate::convert`
1. 取 `RootSymbol` 的 `topInstances`；只接受 module 实例。
2. 对每个 top 实例：
   - `materializeGraph`：按 canonical body 复用或新建 `Graph`。
   - `convertInstanceBody`：真正展开模块体。
   - 标记 top graph，并注册 alias（实例名/模块名）。

## 模块体展开：`convertInstanceBody`
1. 选择 canonical body 作为处理目标；已处理过则直接返回。
2. `ensureBlackboxMemo` 判断是否黑盒：
   - 若 blackbox：完成端口绑定后直接返回，不展开内部。
3. `populatePorts`：生成端口 value 并绑定到 graph。
4.（可选）`emitModulePlaceholder`：阶段性占位。
5. `collectDpiImports` / `collectSignalMemos`：收集 DPI 与信号 memo。
6. `materializeSignalMemos` / `materializeDpiImports`：生成 IR 节点/Value。
7. `ensureWriteBackMemo`：准备写回缓存。
8. `processNetInitializers`：把带 initializer 的 net 转为连续赋值写回。
9. 遍历 `body.members()`，按类型分发：
   - **Instance**：`processInstance`（子模块 elaboration + 在父图创建 kInstance/kBlackbox）。
   - **ContinuousAssign**：`processContinuousAssign`（RHS 转换 + LHS 写回）。
   - **ProceduralBlock**：
     - `always_comb/always @(*)/always_latch` → `processCombAlways`。
     - `always_ff/edge-triggered` → `processSeqAlways`。
     - `initial` → 诊断 warn 并忽略。
   - **InstanceArray / GenerateBlock / GenerateBlockArray**：递归展开。
10. `finalizeWriteBackMemo`：统一生成 `kAssign` / `kRegister` 等写回节点。

## 子流程要点
- **连续赋值**：
  - `CombRHSConverter` 解析 RHS 表达式为 Value。
  - `LHSConverter` 解析 LHS 路径/切片并记录到 `WriteBackMemo`。
  - InOut 端口存在特殊路径（`?:` + `z`）生成 out/oe 写回。
- **组合/时序过程块**：
  - `CombAlwaysConverter` / `SeqAlwaysConverter` 解析控制流与写回逻辑。
  - 使用本地 shadow/write-back 模型避免破坏 SSA。
  - 时序块里的 memory read 会生成 `kMemorySyncReadPort`；SV emit 当前直接输出 `mem[addr]` 的组合读，不额外加一拍寄存器，read 的使能/分支语义由 surrounding mux/guard 保证。
- **实例化**：
  - 子模块先 elaboration；父图创建 kInstance 或 kBlackbox。
  - 端口连接通过 RHS/LHS 解析绑定到父图的 Value。
- **诊断**：
  - 遇到不支持特性时发出 Error/TODO/Warning，Error 会终止 elaboration。

## 当前流程的可见边界（粗线条）
- generate block array 有 Error 分支。
- initial 块被忽略（non-synthesizable）。
- 部分复杂语法仍通过诊断提示后续扩展。
