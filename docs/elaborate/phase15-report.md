# 阶段15：CombAlwaysConverter 循环展开

## 完成内容
- **算法规划文档（KR1）**：在 `docs/elaborate/process-comb-loops.md` 描述 for/foreach 的展开流程、shadow / write-back 协作模型、break/continue 语义以及测试与 JSON 可视化入口，为团队提供统一的循环处理约束与诊断参考。
- **循环展开实现（KR2）**：`CombAlwaysConverter` 新增 loop scope、迭代变量 eval-context 注入与 `LoopContextGuard`。`visitForLoop` 编译期执行 init/cond/step，`visitForeachLoop` 基于已知维度递归展开，所有迭代直接在 shadow 帧中串联并写回写意图，最终只保留 SSA 结构。
- **静态 break / continue（KR3）**：在 `visitStatement` 中捕获 `StatementKind::Break/Continue`，通过 `LoopControl` 标记静态上下文下的退出/跳过请求；循环执行器在每次迭代后检查 pending control，裁剪后续迭代并阻止非静态使用，保持 elaboration 期语义可判定。
- **测试用例与断言（KR4）**：`tests/data/elaborate/comb_always.sv` 新增 `comb_always_stage15_for/foreach/break/continue`。`tests/elaborate/test_elaborate_comb_always.cpp` 解析 JSON artifact，验证 for 形成包含 `data_even/data_odd` 的 `kOr` 树、foreach 形成 `kXor` 树并引用 `src0/src1`，以及 break/continue 输出匹配预期切换。`ctest -R elaborate-comb-always` 作为阶段回归入口。

## 算法与数据结构细节
- **LoopDescriptor**：用 `ForLoopVarState` 与 `ForeachDimState` 捕获迭代变量、范围与步长，存入 loop-scope 栈，确保 shadow 与 RHSConverter 可以在同一帧内查询当前迭代值。
- **EvalContext 复用**：增加 `ensureLoopEvalContext`，在 for 初始化及条件求值时注入最新迭代变量值，避免与常规 RHS EvalContext 混淆。
- **Shadow 整合**：每次循环迭代使用 `runWithShadowFrame(seed, body, /*isStatic=*/false)` 生成独立帧，执行完毕后按顺序 merge 到当前帧，同时保留 `pendingLoopControl_` 来剪枝剩余迭代。
- **CombAlwaysRHSConverter 增强**：`handleCustomNamedValue` 改写为查询 `CombAlwaysConverter::lookupLoopValue`，从而允许 RHS 表达式直接读取迭代变量常量，并在 foreach 场景下把 dummy 维度映射到具体源端口。

## 测试记录
- `cd build && cmake --build . --target elaborate-comb-always`
- `ctest -R elaborate-comb-always --output-on-failure`

## 后续建议
1. **循环 trace 导出**：目前 JSON 中仅含最终 SSA，可在 artifact 增加 per-iteration trace（如 `loopTrace` 数组），便于调试 break/continue 的裁剪效果。
2. **while/do 支持策略**：现阶段对非静态可解循环直接报 TODO，可进一步研究在限定迭代上限下的展开或报错信息改进，明确为何不支持及如何改写。
