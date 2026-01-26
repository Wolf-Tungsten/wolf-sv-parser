# 阶段20：SeqAlwaysConverter 处理循环语句

## 完成内容
- KR1（循环语句建模）：为顺序 always 路径实现 `for/foreach` 的控制流支持，循环在 elaboration 期按 Yosys 的做法静态展开为线性语句序列，维持源代码的执行顺序与优先级。
- KR2（编译期展开）：
  - `for`：要求内联声明迭代变量、`init/cond/step` 可常量求值；在展开时为每次迭代复用 eval-context，将迭代变量绑定为常量参与表达式折叠；加入迭代上限 `kMaxLoopIterations=4096` 防止失控。
  - `foreach`：要求维度范围静态可解，按维度起止与步长递归展开；为每个维度的迭代变量建立局部绑定，保证 RHS/LHS 能读取到该常量索引。
  - 展开后所有写入仍走 shadow + write-back 流程，按“最后一次写覆盖”合并，等价于源循环顺序执行的语义。
- KR3（静态 break/continue）：在静态上下文中捕获 `break/continue`，以 pending 控制标记裁剪后续迭代（break）或跳过当前迭代剩余语句（continue）。若控制条件无法在 elaboration 期判定为常量，则给出 NYI 诊断并保持安全退出。
- KR4（测试样例）：`tests/data/elaborate/seq_always.sv` 新增 `seq_stage20_*` 三个模块；`tests/elaborate/test_elaborate_seq_always.cpp` 增加断言，覆盖“最后写覆盖”、部分位更新保持（Q hold）与循环内 memory 端口数量检查。`ctest -R elaborate-seq-always` 全量回归通过。

## 核心实现要点
- 循环框架与上下文：
  - `AlwaysConverter::visitForLoop/visitForeachLoop` 负责循环展开；`LoopContextGuard` 标记可中断的最近循环层级；`LoopScopeGuard` 管理迭代变量在 eval-context 与本地映射中的 push/pop。
  - 迭代变量绑定存入 `loopValueMap_`，`seedEvalContextWithLoopValues` 在常量求值时注入绑定，允许 `i==1` 等条件在展开期静态决策。
  - 通过 `pendingLoopControl_` + `pendingLoopDepth_` 处理 `break/continue` 的裁剪与跳过；仅允许在静态控制上下文中使用（`currentContextStatic()`）。
- for 展开：
  - `prepareForLoopState` 编译期求值 init/cond/step 并校验为整数；`evaluateForLoopCondition` 每次迭代重算退出条件；当迭代计数超过上限时报 NYI 诊断。
  - 每次迭代体通过 `visitStatement` 在当前 shadow 帧内执行；迭代结束后根据 pending 控制决定是否继续。
- foreach 展开：
  - 解析每个维度为 `ForeachDimState{loopVar,start,stop,step}`；`runForeachRecursive` 以深度优先方式枚举多维组合，逐层绑定 `loopVar` 并执行循环体。
  - 支持在 body 内对寄存器 bit/切片赋值，未写位段在最终写回时由 `createHoldSlice` 保持上一拍值。
- 与阶段17/18/19 的协作：
  - 寄存器写入仍由 write-back memo 汇总，最终在 `SeqAlwaysConverter::finalizeRegisterWrites` 拼接/覆盖后接入 `kRegister*` 的 data 端口；循环内多次 NBA 的“最后写覆盖”由 slices 排序与覆盖规则保证。
  - 循环体内的 memory 访问沿用阶段18/19：整字/按位写记录为 intent（必要时附带 enable/bit mask），同步读生成 `kMemorySyncReadPort`；展开导致的多次写读对应生成多个端口，顺序等于源迭代顺序。
- 诊断与安全：
  - 不支持的循环形态（缺少内联变量、条件/步长非常量、while/do/repeat/forever）统一通过 `reportControlFlowTodo(\"loop\")` 或 NYI 诊断提示。
  - 静态性不足的 `break/continue` 明确报错信息，避免生成不可综合的时序行为。

## 测试记录
- 构建：`cmake --build build`
- 运行：`cd build && ctest -R elaborate-seq-always --output-on-failure`
- 结果：1/1 通过（同时覆盖阶段17/18/19/20 的断言）
  - `seq_stage20_for_last_write`：for+continue，验证寄存器最终 data 等于 `d2`，且不应再依赖 `d0`。
  - `seq_stage20_foreach_partial`：foreach+静态 break，验证拼接为 `concat(hold(Q[7:4]), d[3:0])`。
  - `seq_stage20_for_memory`：for 内 memory 多次写与读，验证生成 2 个 `kMemoryWritePort` 与 1 个 `kMemorySyncReadPort`。

## 影响评估
- 与阶段17/18/19 兼容：复位提取、使能/掩码推断与寄存器 hold 语义在展开后保持一致；既有用例全部回归通过。
- 仅在顺序 always 路径生效；组合 always 的循环支持仍由阶段15负责，二者共享大部分控制流基础设施，但写回/保持语义不同。
- 对大规模常量可解循环会线性增长图规模；通过迭代上限与诊断降低意外爆炸风险。

## 后续建议
1. 扩展 while/do/repeat/forever 的静态展开与诊断策略，至少给出明确的不可综合提示与建议改写。
2. foreach 覆盖更复杂的 packed/unpacked 组合与结构体字段遍历，补充 TypeHelper 层面的索引到 bit-span 映射。
3. 支持循环标签与跨层级 `break/continue` 的目标校验；当前仅支持最近层级。
4. 循环内 memory intent 的聚合与冲突检测：同地址多次 bit/word 写的合并、顺序冲突提示与可选的端口聚合优化。
5. 为展开期的静态决策与裁剪添加更细粒度的可视化 artifact（每次迭代的 trace），便于调试复杂嵌套循环。

