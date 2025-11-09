# Elaborate 阶段9 完成报告

## Objective 进展
- 基于阶段8的 `RHSConverter` 引入 `CombRHSConverter` 派生类，承接组合逻辑 RHS 的全部 lowering；`convertElementSelect`/`convertRangeSelect`/`convertMemberAccess` 被派生类重写，确保结构体/数组/内存读取不再报 NYI，而是生成规范的 `kSlice*` / `kMemoryAsyncReadPort` 操作。
- `RHSConverter` 本体新增扩展钩子：`handleMemoEntry`、`convert*Select`、`convertMemberAccess` 允许子类在 memo 命中时自定义 Value 取法；`TypeInfo`、`createTemporaryValue` 等 helper 上移到 protected，派生类可直接复用。
- `tests/data/elaborate/rhs_converter.sv` 拓展为综合示例：包含 packed struct、packed/unpacked 数组、`+: / -:` 范围、异步内存读端口等典型组合 RHS；`tests/elaborate/test_elaborate_rhs_converter.cpp` 替换为 `CombRHSConverter` 并新增断言覆盖 slice attribute、array index 绑定、memSymbol 绑定与 JSON artifact。

## 数据结构与算法
- **SliceRange / TypeHelper 映射**：`CombRHSConverter::SliceRange` 捕捉结构体字段的 `[msb:lsb]`，通过 `deriveStructFieldSlice` 顺序遍历 `Scope::membersOfType<FieldSymbol>` 并结合 `Type::getBitstreamWidth()` 复原 `TypeHelper` flatten 结果。这样 member 访问可直接映射成 `kSliceStatic(sliceStart/sliceEnd)`，无需重新计算位宽。
- **静态/动态切片建模**：
  - `buildStaticSlice` / `buildDynamicSlice` 抽象出 `kSliceStatic`、`kSliceDynamic` 构造流程，统一写 attribute（`sliceStart`/`sliceEnd` 或 `sliceWidth`）并返回 SSA Value。`RangeSelectionKind::Simple` 固化为 static slice，`Indexed{Up,Down}` 转换为 dynamic slice，其中 `-:` 通过 `createIntConstant(width-1)` + `kSub` 推导起始偏移。
  - `buildArraySlice` 对扁平数组读取生成 `kSliceArray(input,index)`，attribute `sliceWidth` 由 `deriveTypeInfo(expr.type)` 计算，确保多维数组 flatten 后仍能按元素位宽定位。
- **Memo 关联 / 内存读取**：`findMemoEntryFromExpression` 利用阶段6的 memo 表在 element select 时定位原始 symbol；若 entry 的 `stateOp->kind()` 是 `kMemory`，则 `buildMemoryRead` 会创建 `kMemoryAsyncReadPort`，设置 `memSymbol=entry.stateOp->symbol()` 并把地址 operand 指向 RHS 中的选择信号，实现对 unpacked reg/内存的异步读取。
- **常量表达式保障**：`createIntConstant` 统一生成临时常量 Value，bit 宽派生自 selector 的 Type，保证动态切片、`-:` 起点等辅助计算不会触发 graph 中的未定义 Value。

## 测试与验证
- `rhs_converter_case` 追加 `rhs_struct_t`、`net_array`、`reg_mem` 等信号，包含：
  1. `struct_hi_slice = struct_bus.hi` → 要产生指向 memo `struct_bus` 的 `kSliceStatic`。
  2. `static_slice_res = range_bus[11:4]`、`dynamic_slice_res = range_bus[dyn_offset +: 8]` → 覆盖静态 / `+:` 切片。
  3. `array_slice_res = net_array[array_index]` → 验证 `kSliceArray` 绑定 memo value 与 index SSA。
  4. `mem_read_res = reg_mem[mem_addr]` → 检查 `kMemoryAsyncReadPort` 的 operand/attribute。
- `test_elaborate_rhs_converter` 先运行 Elaborate，复用阶段8的 RHS map，换成 `CombRHSConverter` 执行转换；新增断言：
  - slice 操作的 `sliceStart`/`sliceEnd`/`sliceWidth` 与期望匹配。
  - `kSliceArray` 的两个 operands 分别绑定 memo value 与 `array_index`。
  - `kMemoryAsyncReadPort` operand 等于 `mem_addr`，attribute `memSymbol` 精确指向 `reg_mem` 的 `kMemory` Operation。
  - 其余历史断言（kAdd/kMux/kReplicate/kReduce 等）全部保留，确保新实现没有回归。
- CTest 继续以 `elaborate-rhs-converter` 目标运行，并将完整 GRH netlist 序列化到 `${ELABORATE_ARTIFACT_DIR}/rhs_converter.json` 供人工审查。

## 后续工作建议
1. 复用 `handleMemoEntry`，在 SeqRHSConverter 中加入对寄存器写口 / kMemory 写口的特化逻辑，避免重复解码 LHS。
2. 扩展 `CombRHSConverter` 支持 streaming concat (`{<<{}}`)、`inside`/`with` 等组合表达式，进一步提升 AST 覆盖率。
3. 将数组/结构体 slice 信息（`SignalMemoField`）缓存到哈希表，降低大结构体多次 member 访问时的线性扫描成本。
