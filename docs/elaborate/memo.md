# Elaborate Memo 数据结构

在 elaborate 管线中，有 4 份互相关联的 memo 结构用来缓存从 slang AST 推导出的模块内部状态，并为后续阶段的转换提供锚点。本文档梳理它们的职责、字段以及生命周期，方便在阶段 10 及以后扩展时作为参照。

## Net Memo
- **定义位置**：`SignalMemoEntry` / `SignalMemoField`，声明于 `include/elaborate.hpp`，实例存放在 `Elaborate::netMemo_`。
- **作用**：缓存 `net/logic` 类信号的扁平化类型信息（总宽度、有符号属性、字段展开路径）、关联到 GRH 中的占位 `grh::Value`，让 RHS/LHS 转换可以直接复用既有 SSA 句柄。
- **关键字段**
  - `symbol` / `type`：指向 slang AST 中的 `ValueSymbol` 及其类型，便于后续做语义/诊断回溯。
  - `width` / `isSigned`：记录 flatten 后的总 bit 宽度与符号属性。
  - `fields`：一组 `SignalMemoField`，描述结构体 / 数组被 flatten 后每个叶子字段的路径、位宽及位段 `[msb:lsb]`。TypeHelper 会保证字段按高位到低位排列，便于切片。
  - `value`：指向 `grh::Graph` 中 materialize 出来的 `Value`。net 类信号不会绑定 `stateOp`。
  - `multiDriver`：标记是否有多个组合过程块驱动该信号，用于后续切片合并时避免生成全宽写回导致冲突。
- **生命周期**
  1. `Elaborate::collectSignalMemos` 遍历 `InstanceBodySymbol` 的成员，过滤出固定位宽的 `NetSymbol` / `VariableSymbol`，并记录驱动类别；
  2. `Elaborate::ensureNetValues` 在模块第一次 elaboration 时，根据 memo 信息创建/复用 `grh::Value`；
  3. `Elaborate::processNetInitializers` 扫描带 initializer 的 wire，使用 Comb RHS 转换初值并借助 WriteBackMemo 生成连续赋值；即便没有显式驱动也会把此类 wire 留在 netMemo 中，保证 RHS 可解析。
  4. 对于无驱动的 `variable`（端口连接或占位场景常见），仍按 net 进入 netMemo，确保 RHS NamedValue 可解析且可参与 comb 合并。
  5. 后续 RHS/LHS 访问通过 `SignalMemoEntry` 里的字段直接找到该 `Value`。

## Reg Memo
- **定义位置**：与 Net Memo 共用 `SignalMemoEntry`，额外使用 `drivingBlock`、`stateOp` 字段，实例存放于 `Elaborate::regMemo_`。
- **作用**：缓存需要用寄存器表达的信号（仅寄存器）。在建图初期会根据 memo 生成对应的寄存器原语（`kRegister` / `kRegisterRst` / `kRegisterArst`），并把 `value`（寄存器的 `q` 端口）与 `stateOp` 写回 entry 供 RHS 使用。
- **关键字段**
  - `drivingBlock`：记录唯一的时序过程块（例如 `always_ff`）指针，便于派生时钟极性等属性。
  - `stateOp`：指向生成的 GRH State Operation（寄存器族）。
  - 其他字段与 Net Memo 相同，用于类型信息与 value 句柄。
- **生命周期**
  1. `collectSignalMemos` 根据驱动分类把属于 `MemoDriverKind::Reg` 的符号加入 reg memo（已剔除 memory 类对象）；
  2. `ensureRegState` 负责在 GRH 图中创建寄存器原语，并把 `value`（寄存器的 `q` 端口）与 `stateOp` 写回；
  3. RHSConverter 读取 reg memo 时，可直接复用 `entry.value`（寄存器的输出）。

## Mem Memo
- **定义位置**：与 Net Memo 共用 `SignalMemoEntry`，实例存放于 `Elaborate::memMemo_`。
- **作用**：缓存需要用片上存储器表达的对象（unpacked reg/array 等）。在建图初期会根据类型推导生成 `kMemory` 占位，并将 `stateOp` 写回 entry，用于后续读写端口的生成。
- **关键字段**
  - `stateOp`：指向生成的 `kMemory` Operation，占位属性包含 `width`（行宽）、`row`（行数）、`isSigned`。
  - 其他字段与 Net Memo 相同；通常内存条目不提供扁平化的 `value`。
- **识别与生命周期**
  1. `collectSignalMemos` 使用 `deriveMemoryLayout(type, symbol)` 识别 unpacked array 等存储器形态，将其加入 mem memo；
  2. `ensureMemState` 负责在 GRH 图中创建 `kMemory` 并写入关键属性，回填 `stateOp`；
  3. RHS/Seq 路径读取 `mem[addr]`/写入 `mem[addr] <= ...` 时，分别生成 `kMemoryAsyncReadPort`/`kMemorySyncReadPort` 与写口（`kMemoryWritePort`/`kMemoryMaskWritePort`）；这些端口的绑定以 mem memo 的 `stateOp` 为锚点。
  4. 查找规则：RHS/LHS converter 的 `findMemoEntry` 已依序在 `netMemo` → `regMemo` → `memMemo` 中查找。

## Write-Back Memo
- **定义位置**：`class WriteBackMemo` 位于 `include/elaborate.hpp`，实例将挂在 `Elaborate::writeBackMemo_`，对应代码实现在 `src/elaborate.cpp`。
- **动机**：SystemVerilog 的 LHS 可以是任意支持切片、层级访问、连接的表达式。如果在解析过程中直接覆写 net/reg memo 中的 `Value`，会破坏 GRH 需要的 SSA。Write-Back Memo 负责暂存「写意图」，待所有连续赋值与过程块遍历完成后，统一生成写回操作。
- **核心元素**
  - `AssignmentKind`：标识写入来自连续赋值（Net）还是过程块（Reg/Sequential），后续阶段可据此决定优先级或报错策略。
  - `Slice`：描述一次写入覆盖的目标位段 `[msb:lsb]`、flatten 后对应路径（便于调试）、右值 `grh::Value*` 以及产生该切片的 AST 节点指针。
  - `Entry`：面向单个 `SignalMemoEntry` 的聚合，包含 assignment kind、origin symbol 以及一个 `Slice` 数组。
- **工作流**
  1. LHS 解析阶段把一条赋值拆成若干 `Slice`，调用 `WriteBackMemo::recordWrite` 绑定到对应的 net/reg memo entry；
  2. `WriteBackMemo::finalize` 会以目标 `SignalMemoEntry::width` 为基准对切片排序/校验，若出现空洞会自动插入宽度匹配的 0 常量补齐，再必要时生成 `kConcat`，最终：
     - 对 net：创建 `kAssign` 把组合出的 SSA Value 写回 `entry.value`；
     - 对 reg：将组合结果连接到 `entry.stateOp` 的数据端（寄存器族 operand），保持寄存器 SSA 输出不被破坏；
     - 对 `kMemory`：不在 Write-Back Memo 中直接写回；顺序 always 中的 memory 写由 SeqAlwaysConverter 记录 `MemoryWriteIntent` 并在 finalize 阶段生成写口。
 3. finalize 结束后 memo 会被清空，保证每次 elaboration 只消费一次 pending writes。

通过上述分层 memo，elaborate 可以把「解析 slang AST」、「构造 GRH 节点」以及「最终写回/驱动 stateful 对象」解耦，既便于诊断，也方便阶段化落地。
