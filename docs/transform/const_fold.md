# ConstantFold Pass 实现说明

本文档面向维护者，介绍当前 ConstantFoldPass 的实现细节、约束与待改进点，便于后续演进时保持一致性。

## 目标与范围
- Pass id：`const-fold`。定位为 Elaborate→Transform→Emit 之间的纯组合常量折叠。
- 作用对象：`kConstant` 之外的纯组合算子（算术/逻辑/位操作、`kAssign`、`kConcat`、`kReplicate`、`kSlice*`、`kMux`）。时序类（寄存器/存储器/instance/dpic 等）不折叠。
- 基础规则：依赖 `slang::SVInt`/`logic_t` 实现 SystemVerilog 位宽/符号/四态语义，按 Value 的 `(width,isSigned)` 规范化 resize。

## 核心流程
1) **常量表种子**：遍历图中 `kConstant`，解析 `constValue` 为 SVInt 后放入 `ConstantStore`（指针→值+hasUnknown）。解析错误视为 fatal。
2) **迭代折叠**：最多 `maxIterations`（默认 8）轮；每轮按 `operationOrder` 扫描。
   - 仅当所有 operands 已在常量表中时尝试折叠；含 X/Z 且 `allowXPropagation=false` 时跳过并记 warning。
   - 支持折叠的 kind：二元（加减乘除模、比较、按位/逻辑与/或/异或、移位）、一元（按位/逻辑非、各类 reduction）、`kAssign`（透传）、`kConcat`、`kReplicate`(需 `rep` 属性)、`kSlice*`（需 sliceStart/sliceEnd/sliceWidth）、`kMux`。
   - 结果按目标 result Value 的 `(width,isSigned)` resize，生成 const literal 采用 `SVInt::toString(LiteralBase::Hex, includeBase=true, width)`。
3) **节点替换与删除**：
   - 为每个折叠结果创建新的 Value + `kConstant` op，命名前缀 `__constfold_*`，放入同一 Graph。
   - 调用 `replaceUsers` 将原 result 的所有用户 operand 替换为新常量，并通过 `Graph::replaceOutputValue` 维护输出端口绑定。
   - 折叠成功的原 op 记入待删列表，单轮完成后统一 `graph.removeOperation`（清理 users/def/operationOrder）。
   - 折叠结束后清理无用户且不绑定端口的 `kConstant` op，避免 emit 输出死 assign（值仍保留）。
4) **诊断与失败条件**：
   - error：常量解析失败、缺失属性、结果/operand 缺失、替换/索引异常、未捕获异常。
   - warning：含未知值且 disallow X 折叠。
   - `PassResult.changed` 在任一轮成功折叠时置位；发生 error 则 `failed=true`。

## 重要辅助函数
- `normalizeToValue(Value, SVInt)`: 按 Value 宽度/符号 resize，保持四态信息。
- `replaceUsers(Graph&, oldValue, newValue, onError)`: 逐用户调用 `replaceOperand`，防御越界并汇报错误。
- `Graph::replaceOutputValue`: 将输出端口从旧 Value 改到新常量，更新端口标志。
- `Graph::removeOperation`: 清理 operands users、results definingOp、operationOrder 后移除 op。

## 已知限制 / 待办
- 不比对已有指针缓存与 symbol 一致性；依赖 upstream GRHVerifyPass 保证图合法。
- 仅处理单结果 op（代码按 results.size() 遍历，但常见折叠算子均单结果）；多结果算子需扩展测试验证。
- X/Z 传播策略简单（全跳过或全允许），未做更精细的四态折叠/短路。
- 新常量不会尝试合并重复 literal；可能引入重复等效常量节点。
- 仍保留旧 Value（仅删除旧 op），未做 Value 级垃圾回收。

## 相关文件
- Pass 实现：`lib/transform/const_fold.cpp`
- Pass 接口与配置：`lib/include/transform/const_fold.hpp`
- 管线集成：`src/main.cpp`
- 测试：`tests/transform/test_const_fold_pass.cpp`
