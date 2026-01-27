# Convert 工作流（动态视角）

> 本文描述 Convert 的运行时流程，从入口到输出按时间顺序展开。

## 入口阶段
1. `ConvertDriver::convert` 作为入口，构建 `ConvertContext`：注入 `Compilation`、`RootSymbol`、选项与 `ConvertDiagnostics`。
2. 初始化 `ConvertLogger`（默认关闭），由选项控制日志级别与过滤规则。
3. 初始化 `Netlist` 与全局缓存（symbol 表、Graph 缓存）。
4. 读取 `RootSymbol::topInstances`，建立顶层实例列表（允许多个 top）。

## 模块计划阶段（每个 top instance 递归）
5. 解析实例的 `InstanceBodySymbol`，生成 `ModulePlan` 骨架；对非黑盒实例基于参数配置进行特化并缓存。
6. **端口/信号收集**：
   - 遍历 `PortSymbol` 与 body 内成员，收集 net/var/mem。
   - 记录方向、位宽、签名、packed/unpacked 维度。
   - inout 端口拆分为 `in/out/oe` 三路计划。
7. **类型与常量解析**：
   - 使用 `slang::ast::Type` 计算位宽与 signed。
   - 对参数与常量表达式求值，固化用于 width/array bounds 的常量。

## 读写关系分析阶段
8. **连续赋值扫描**：
   - 记录 LHS 写入与 RHS 读取，标记为 combinational 驱动。
9. **过程块扫描**：
   - 区分 `always_comb` / `always_latch` / `always_ff` 与显式敏感表。
   - 识别可综合控制流（if/for/case）并构建 guard/优先级信息。
   - 提取时钟/复位/使能语义，并形成 `rwOps` 事件。
10. **memory 端口归类**：
   - 组合读 -> `kMemoryAsyncReadPort`。
   - 时序读 -> `kMemorySyncReadPort*`（读寄存器复位语义明确）。
   - 写入 -> `kMemoryWritePort` / `kMemoryMaskWritePort`。

## Lowering 与写回阶段
11. **表达式 Lowering**：
    - RHS 表达式降级为 GRH Value/Op，保持四态逻辑语义。
12. **LHS 路径解析**：
    - 解析切片、拼接、数组索引，形成可写目标或 memory 端口。
13. **写回计划汇总**：
    - 同一信号多路写入合并为 guard/mux 结构。
    - 生成 kAssign/kRegister/kLatch 等写回节点。

## 图构建与收尾阶段
14. **Graph 组装**：
    - 根据 `ModulePlan` 创建 Value/Operation，绑定端口与 inout 逻辑。
15. **实例化生成**：
    - 为子模块生成 kInstance/kBlackbox，并绑定端口连接。
    - 保留层次化结构，不扁平化实例树。
16. **Netlist 收尾**：
    - 标记 top graphs，注册 module/alias 名称。
17. **可选校验**：
    - 运行 GRH verify/const-fold 等 pass（若启用），汇总诊断信息。

## 输出
18. 返回 `Netlist` 给 emit/transform 等下游，诊断信息交由 CLI 输出。
