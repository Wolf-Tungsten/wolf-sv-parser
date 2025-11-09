# Elaborate 阶段10 完成报告

## Objective 进展
- 设计并在 `include/elaborate.hpp` 内落地 `WriteBackMemo`，将连续赋值与过程块的写意图抽象为 `AssignmentKind + Entry + Slice` 三层结构，提供 `recordWrite/clear/finalize` 对外 API（include/elaborate.hpp:111-155）。
- 在 `src/elaborate.cpp` 中完整实现 memo 生命周期：`recordWrite` 负责聚合切片、`composeSlices` 排序并补零、`attachToTarget` 根据 target 是 net 还是 reg 生成 `kAssign` 或回填寄存器数据端，`finalize` 则遍历所有 entry 并清空 pending writes（src/elaborate.cpp:639-882）。
- Elaborate 主流程在每个 `InstanceBodySymbol` 开始/结束时创建并收敛对应的 write-back memo，保证未来 LHS 解析可以自由追加切片且不会泄漏跨模块状态（src/elaborate.cpp:1885-1925, src/elaborate.cpp:2259-2269）。
- 以 `docs/elaborate/memo.md` 文档方式同步三类 memo（net/reg/write-back）的字段、生命周期与交互，为后续阶段提供查阅依据（docs/elaborate/memo.md:1-44）。

## 数据结构与算法
- **切片聚合**：每条写入会被拆成 `Slice{path, msb, lsb, value}`，`composeSlices` 先按 `msb -> lsb` 逆序排序，再依据目标位宽维护 `expectedMsb`，在切片之间或首尾出现空洞时自动调用 `createZeroValue` 生成常量填充，最后根据组件数量决定复用单个 Value 或创建 `kConcat`（src/elaborate.cpp:704-793）。
- **诊断与命名**：`makeOperationName/makeValueName` 使用目标 symbol 作为前缀并携带自增计数，`reportIssue` 统一把任何缺失 memo、宽度不匹配、重复驱动等问题映射到 origin symbol 的 NYI 诊断，方便阶段11接手时复现上下文（src/elaborate.cpp:654-702）。
- **写回目标区分**：`attachToTarget` 根据 memo entry 是否含 `stateOp` 自动切换行为：net 走 `kAssign` 写回占位 Value，reg 则直接把组合好的 Value 作为 `kRegister` 的 data operand，同时校验宽度一致性并显式对 `kMemory` 报告 NYI（src/elaborate.cpp:810-858）。
- **管线集成**：`Elaborate::ensureWriteBackMemo` 以 `InstanceBodySymbol*` 为 key 缓存 memo，`finalizeWriteBackMemo` 在 body 解析结束后触发 `WriteBackMemo::finalize`，从而保证 assign 解析只需调用 `recordWrite`，其余细节由统一收敛逻辑处理（src/elaborate.cpp:2259-2269）。

## 测试与验证
- 新增 `tests/elaborate/test_write_back_memo.cpp`，构造纯 C++ GRH 图分别验证：1) 多切片 net 会生成 `kConcat` + `kAssign`；2) reg 写回把组合值直接接到 `kRegister` data；3) 仅覆盖低位时会自动插入零填充并继续生成合法写回（tests/elaborate/test_write_back_memo.cpp:15-188）。
- 将测试加入 CTest，保障 `write-back memo` 作为独立可执行目标运行，任何未来对 compose/attach 逻辑的更改都会即时暴露（CMakeLists.txt:142-151）。

## 后续工作建议
1. 将连续赋值与过程块的 LHS 解析器接入 `WriteBackMemo::recordWrite`，使阶段11能够真正消费该基础设施。
2. 扩展 `attachToTarget` 支持 `kMemory`（当前返回 NYI），并为多写端口/掩码语义引入合适的冲突检测。
3. 引入 LHS 覆盖冲突和优先级检查，例如同一位段被多次写入时的报错策略，以便在 finalize 之前就能阻止无效图生成。
