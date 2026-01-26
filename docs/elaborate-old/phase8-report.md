# Elaborate 阶段8 完成报告

## Objective 进展
- 引入 `RHSConverter` 基类集中处理“RHS 表达式 → GRH Operation/Value 链”，Context 同时携带 `grh::Graph`、net/reg memo snapshot、origin symbol 与诊断接口，确保转换过程中任何 memo 读取都能引用已有 SSA value，避免重复建图。
- `RHSConverter` 内聚命名/缓存/常量求值三套基础设施：
  - `valueCounter_/operationCounter_` 生成唯一符号，`std::unordered_map<const Expression*, grh::Value*>` 复用共享子表达式；
  - `TypeInfo {width,isSigned}` 自 `Type::getBitstreamWidth()` 派生 bit 宽并裁剪到 `int64_t` 范围；
  - `slang::ast::EvalContext` 被懒加载，`evaluateConstantInt` 复用它求 `replicate` 等常量参数。
- `convert*` 系列函数覆盖 literal、NamedValue、一元、二元、?:、拼接、复制、显式转换等 RHS 节点，分别落到 `grh::OperationKind::kConstant/kAdd/kMux/kConcat/kReplicate/kAssign` 等基本算子，并按需写 attribute（如 `rep`、`constValue`）；遇到 RangeSelect 等未覆盖节点则依据 origin 打 NYI 诊断，便于后续扩展。
- 常量属性统一调用 `SVInt::toString(base=Hex, includeBase=true)`，所有测试和未来 pass 都能读取标准 Verilog Literal 形式（例如 `8'haa`）。

## 数据结构与算法
- **Memo 绑定**：RHSConverter 通过 `findMemoEntry` 在 `std::span<const SignalMemoEntry>` 中按 symbol 指针线性查找，命中后直接返回 `entry.value`。对于 `kRegister*`，若 memo 尚未缓存 `entry.value`，则自动回退到 `entry.stateOp->results()[0]`（寄存器的 `q`），保证所有 reg 读取都能共享同一 SSA result；若仍缺失则携带 origin 产生日志。
- **Operation 构建**：`buildUnaryOp`/`buildBinaryOp`/`buildMux`/`buildAssign` 等 helper 只依赖 `TypeInfo` 和目标 `OperationKind`，在 Graph 内即时创建 Operation 并绑定 result Value。这样 Comb/Seq 派生类可覆写命名或注入额外 operand，而无需复制 value/materialization 过程。
- **常量评估**：复制次数等整型常量统一走 `evaluateConstantInt`，利用 `EvalContext` 的 `Expression::eval` 结果再做 `SVInt::as<int64_t>` 安全转换；若表达式包含 X/Z 或超范围，直接返回 `std::nullopt`，调用者落到 reportUnsupported，保证后续扩展可区分“表达式太复杂”与“我们尚未实现”。
- **结果缓存**：`cache_` 以 Expression* 为 key，避免在同一 RHS 树内重复创建相同操作（尤其是组合 mux/逻辑多次复用的子表达式），保持 Graph 中 SSA 结构简洁。

## 测试与验证
- 新增 `tests/data/elaborate/rhs_converter.sv`，覆盖加减、逻辑与或、条件、kConcat/kReplicate、kReduce、常量等典型 RHS 形态。
- 新增 `tests/elaborate/test_elaborate_rhs_converter.cpp`：
  - 通过 slang Driver 建立 Compilation → Elaborate → Graph；
  - 针对每个 RHS 赋值抓取 `AssignmentExpression::right()`，调用 RHSConverter；
  - 验证生成的 GRH 结构是否符合预期：如 kAdd/kMux/kReplicate/kConstant/kReduceAnd/kXor 等的 operands/result 均指向 memo value，`rep` attribute=4、`constValue=8'haa`、kAdd 复用 `net_a/net_b` memo entry，以及 `seq_reg`（reg memo） 的寄存器输出被 `reg_use` 的 kAdd 直接复用；额外将完整 `grh::Netlist` pretty JSON 写入 `${ELABORATE_ARTIFACT_DIR}/rhs_converter.json` 供人工核查。
- CMake 引入 `elaborate-rhs-converter` 目标并加入 CTest，保障阶段回归和后续扩展均可复用同一基线。

## 后续工作建议
- 在 RHSConverter 基类中继续扩展 `convertRangeSelect`、`convertMemberAccess`、`convertStreamingConcat` 等函数，将结构体/数组访问映射到 `kSlice*` 系列，使 Combo/Seq 派生类只需关注语境差异。
- 设计 `AssignmentConverter`：负责 LHS 解析、阻塞/非阻塞语义、驱动 fan-out、时序控制，并在内部调用 RHSConverter 处理右值；当前基类已剥离 `AssignmentExpression` 支持，方便后续直接嵌入。
