# 阶段19：SeqAlwaysConverter 处理 if/case 语句

## 完成内容
- KR1（分支解析）：
  - 顺序 always 路径支持 `if`/`case` 分支的静态折叠与动态合流：静态恒真/恒假的条件直接选择分支；动态条件通过在 `AlwaysConverter::mergeShadowFrames` 生成 `kMux` 合流。
  - 新增布尔辅助：`buildLogicAnd`/`buildLogicNot` 与 `coerceToCondition`（将多位表达式规约为 1-bit 条件），并复用已有 `buildEquality`/`buildLogicOr`/`buildWildcardEquality`。
  - 为顺序分支引入 guard 栈（currentGuard）：进入真/假分支分别将 guard 与条件与/取反后压栈，`case` 则使用每个分支的匹配条件；default 分支使用 `~(match_0 | ... | match_n)`。
- KR2（寄存器复位/使能）：
  - 复位沿用阶段17：`buildResetContext` + `extractResetBranches` 从数据路的 `kMux` 中提取 `rst` 与 `resetValue`，分别接入 `kRegisterRst/kRegisterARst`。
  - 使能通过保持语义表达：分支缺省不写时，顺序合流自动以 `Q` 作为 hold，形成 `mux(en, new_data, Q)` 或等价 concat/hold 结构（`SeqAlwaysConverter::createHoldSlice` 在需要时生成保持片段）。典型 `if (en) r <= d;` 产出 `kMux(en, d, Q)`。
- KR3（内存端口使能与掩码）：
  - LHS 检测到 `mem[...] <= ...` 或 `mem[...][i] <= b` 时记录 `MemoryWriteIntent`/`MemoryBitWriteIntent`，将当前 guard 作为 `enable` 保存；Finalize 阶段创建 `kMemoryWritePort`/`kMemoryMaskWritePort` 并连接 `clk/addr/enable/data(/mask)`。
  - RHS 识别 `mem[addr]` 在顺序 always 中生成 `kMemorySyncReadPort`，其 `en` 同样使用当前 guard；若无显式 guard 则退化为常量 1。
  - `case/casez` 分支分别以各自匹配结果作为不同写端口/掩码端口的 `enable`；`casez/casex` 的通配由 `buildWildcardEquality` 生成。
- KR4（测试样例）：
  - 在 `tests/data/elaborate/seq_always.sv` 新增以下模块：
    1) `seq_stage19_if_en_reg`：`if (en) r <= d;`
    2) `seq_stage19_if_en_mem`：整字写/按位写/同步读分别由 `en_wr/en_bit/en_rd` 控制
    3) `seq_stage19_case_mem`：`case(sel)` 不同分支写不同端口，`default` 不写
    4) `seq_stage19_casez_mem`：`casez(sel)` 通配写两条端口
    5) `seq_stage19_rst_en_reg`：`if (rst) r<=0; else if (en) r<=d;`
  - 在 `tests/elaborate/test_elaborate_seq_always.cpp` 扩展断言：
    - 寄存器 NBA：`kRegister*` 的 data 为 `kMux(en, d, Q)` 或等价结构；复位端口/值正确；
    - Memory：`kMemorySyncReadPort/kMemoryWritePort/kMemoryMaskWritePort` 的 `en` 分别能“提及”对应输入（`en_rd/en_wr/en_bit`），掩码端口的位移/拼接结构正确；
    - `case/casez`：不同端口的 `enable` 分别来自 `eq(sel, const)/wildcard` 产物。

## 核心实现要点
- Guard 设计与合流：
  - `AlwaysConverter` 新增 `guardStack_` 与 `pushGuard/popGuard/currentGuardValue()`；顺序分支进入子帧时压入 `parentGuard & cond`（或 `& ~cond`），`case` 分支使用 `match_i`；default 使用 `~(match_0 | ... | match_n)`。
  - `mergeShadowFrames` 在顺序模式下允许覆盖不完整，缺失分支使用 `entry.value (Q)` 作为 hold，再以 `kMux(cond, trueValue, falseValue)` 合流，回填为“全宽片段”。组合模式保持原先“覆盖完整性”校验。
- 内存端口：
  - LHS 记录时绑定 `enable`；Finalize 时逐 intent 生成端口，并将 `enable` 规约为 1-bit 后接入；按位写继续使用 `buildShiftedBitValue/buildShiftedMask` 合成 data/mask。
  - 同步读口 `buildMemorySyncRead` 支持 `enableOverride`，优先使用 guard，否则接常量 1。
- 命名唯一性修复：
  - `AlwaysConverter` 为控制流临时节点引入 `controlInstanceId_` 与本地计数，`makeControlOpName/ValueName` 中加入实例 id + 计数，避免不同 always 块/多次进入生成相同符号导致的 `Duplicated operation symbol`。

## 测试记录
- 构建：`cmake --build build`
- 运行：`cd build && ctest -R elaborate-seq-always --output-on-failure`
- 结果：1/1 通过（包含阶段17/18/19 相关断言）

## 影响评估
- 组合 always 路径保持原行为与校验（覆盖完整性、latch 告警不变）。
- 顺序 always 新增 guard + 合流的行为只在 procedural 语义下生效；既有阶段17/18 用例通过。
- 内存端口 `enable` 由 if/case 语义推导，对未出现 guard 的情况仍退化为 `1`，与阶段18 兼容。

## 后续建议
1. 端口聚合优化：同一 address 的多条 `mask write` 可在 finalize 阶段做聚合，减少图规模。
2. 更强的 `casez/casex` 结构校验：测试当前只检查 enable “提及” `sel`，可在测试中匹配完整的 `xor + and(mask) + eq0` 结构。
3. 复位/使能混合的复杂形态：拓展 `extractResetBranches` 支持更多层级/嵌套 if/case 的 reset 值提取。
4. 诊断增强：为顺序分支下的潜在冲突（同地址重复整字写/掩码与整字交错写）提供更明确告警。

