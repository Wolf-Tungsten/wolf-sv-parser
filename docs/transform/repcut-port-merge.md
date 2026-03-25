# repcut-port-merge

## 目标

`repcut-port-merge` 是一个专门服务于 `repcut` 后端的后处理 pass。

它的目标不是改变逻辑功能，而是把 wrapper graph 中“连接拓扑完全相同”的细粒度跨单元 port，合并成较少数量的宽 port，从而减少后续：

- wrapper graph 顶层跨单元 link 数量
- 分区 graph 的 input/output port 数量
- `emitVerilatorRepCutPackage` 生成的 `manifest.connections` 数量
- 运行时 scatter / gather 热路径中的逐信号拷贝语句数

这里的“单元”包括 `part_*`，也可以包括 `debug_part`。唯一硬约束是：**任何涉及原始顶层输入/输出/inout 的 port 都不能动**。

## 放置位置

这个 pass 应该放在 `repcut` 完成分区重建之后执行，而不是在分区前执行。

原因很直接：

- 只有在 `repcut` phase-e rebuild 之后，wrapper graph 中的跨分区连接关系才被完全显式化
- 此时每条跨分区 link 已经变成顶层 value，并且已经知道：
  - 谁是 producer unit
  - 哪些 unit 是 sink
  - 哪些 link 同时还绑定到了 top port

当前这部分重建逻辑就在 [`repcut.cpp`](/workspace/wolvrix-playground/wolvrix/lib/transform/repcut.cpp#L4768) 附近。

建议有两种接入方式：

1. 作为独立 pass，要求在 `repcut` 之后运行
2. 直接作为 `repcut` phase-e rebuild 之后的内部收尾步骤

从实现和使用稳定性看，第二种更合适，因为它天然只作用在 repcut 重建出来的 wrapper graph 上。

## 只处理什么

只处理 wrapper graph 中的 **纯 `unit_to_unit` link**。

具体地说，一条 top-level value 只有同时满足下面条件，才允许参与 merge：

- 它的 driver 是某个 unit instance 的 output
- 它的 sink 全部是某些 unit instance 的 input
- 它 **没有** 绑定到 wrapper top input/output/inout
- 它不是常量直驱 top，也不是 top input 直驱 unit

换句话说，要明确排除：

- `top_to_unit`
- `unit_to_top`
- 任意涉及 inout 的连接
- 任意与原始 top port 别名复用的 value

这和当前 emit 侧的 edge 分类是一致的，见 [`verilator_repcut_package.cpp`](/workspace/wolvrix-playground/wolvrix/lib/emit/verilator_repcut_package.cpp#L2140) 到 [`verilator_repcut_package.cpp`](/workspace/wolvrix-playground/wolvrix/lib/emit/verilator_repcut_package.cpp#L2212) 一段的 driver/sink 分类。

## “相同连接拓扑结构”的定义

建议按下面这个签名分组：

- `producer_instance`
- `sink_instance_set`

也就是一组 link 只有在“由同一个 unit 输出，并且流向完全相同的一组 sink unit”时才允许合并。

这里的 `sink_instance_set` 要求是精确相同，不是子集，不是交集。

例如：

- `part_7 -> {part_12}` 和 `part_7 -> {part_12}` 可以合并
- `part_7 -> {debug_part}` 和 `part_7 -> {debug_part}` 可以合并
- `part_7 -> {part_12, debug_part}` 和 `part_7 -> {part_12}` 不能合并
- `part_7 -> {part_12}` 和 `part_8 -> {part_12}` 不能合并

这是最稳的定义，因为 merge 后仍然只需要一份 producer 端打包逻辑，以及在完全相同的一组 sink 端做解包。

## 为什么不按位宽分组

不需要要求原始 link 等宽。

位宽不同也可以合并到一个 packed bus 里，只要记录每个成员的：

- `offset`
- `width`

然后 producer 侧 `concat`，consumer 侧 `slice` 即可。

因此决定是否同组的关键不是 width，而是连接拓扑。

## 图改写方式

这个 pass 本质上是“宽总线打包/解包”。

### 1. 在 wrapper graph 上识别 merge group

遍历 wrapper graph 顶层的 `kInstance`：

- 收集每个 output value 的 driver unit
- 收集每个 value 的 sink unit 列表
- 标记哪些 value 与 top port 有关联

对满足条件的 `unit_to_unit` value，按 `(producer_instance, sink_instance_set)` 分组。

组内成员按稳定顺序排序，建议用：

1. 原 top-level value 名
2. 若重名则按 value id

这样 emit 出来的 port 顺序稳定，便于 diff 和缓存。

### 2. 在 producer graph 中新增 merged output port

假设某组成员是：

- `a[2:0]`
- `b[0:0]`
- `c[15:0]`

则在 producer 对应的 partition graph 内：

- 新建一个宽度为 `3 + 1 + 16 = 20` 的 value
- 新建一个 `kConcat`
- 按固定顺序把成员 port value 接入 `kConcat`
- 用 `kConcat` 结果绑定新的 output port，例如 `pm_out_0`

随后删除这一组原有的多个 output port。

注意：这里只是删除 port 绑定，不删除这些原值本身，因为它们在 graph 内仍然是原逻辑的结果。

### 3. 在每个 consumer graph 中新增 merged input port

对同一组的每个 sink unit：

- 新建一个同宽 input value
- 绑定新的 input port，例如 `pm_in_0`
- 针对组内每个成员，创建 `kSliceStatic`
- slice 出对应 bit range 后，替换该原 input port value 在图内的全部 uses

随后删除这一组原有的多个 input port。

这里等价于把多个“单根输入线”变成“一根宽线 + 若干局部 slice”。

### 4. 在 wrapper graph 中重连实例端口

producer instance：

- 去掉原来多个 output mapping
- 增加一个 merged output mapping，连接到新的 top-level merged value

每个 consumer instance：

- 去掉原来多个 input mapping
- 增加一个 merged input mapping，连接到同一个 merged value

最后删除这一组原来的多个 top-level link value，以及与之相关的 alias assign。

### 5. wrapper instance 不建议原地修改，建议重建

这里有一个实现细节很重要：

- wrapper 顶层的 `kInstance` 不只是“连了几个 value”
- 它还带有 `inputPortName` / `outputPortName` 属性
- operand/result 的顺序必须和这些属性严格对齐

因此 `repcut-port-merge` 不适合只对旧 instance op 做零散 patch。

更稳的方式是：

1. 读取旧 instance 的 `moduleName` / `instanceName`
2. 根据 merge 后的子图 port 列表，生成新的 `inputMapping` / `outputMapping`
3. 用和 `repcut` phase-e rebuild 相同的 `buildInstance(...)` 逻辑创建一个新 instance op
4. 删掉旧 instance op

这样不会留下属性顺序和 operand/result 顺序不一致的问题。

## 建议的数据结构

建议 pass 内部至少维护下面几类结构。

### LinkDesc

描述 wrapper 顶层的一条候选 link。

- `topValue`
- `width`
- `isSigned`
- `producerInstance`
- `producerPortName`
- `sinkPorts`
- `touchesTop`

其中 `sinkPorts` 是一组：

- `instanceName`
- `portName`

### GroupKey

作为 merge 分组键：

- `producerInstance`
- `sinkInstanceSet`

这里 `sinkInstanceSet` 只记录实例名集合，不记录具体 port 名，因为 port 名只是组内成员信息，不参与判定“拓扑是否相同”。

### GroupMember

组内每个被合并成员：

- `topValue`
- `width`
- `isSigned`
- `producerPortName`
- `sinkPorts`
- `offset`

其中 `offset` 是 packed bus 中的 bit 起始位置。

### UnitRewritePlan

按 unit instance 维度收集改写动作。

对 producer：

- `removedOutputPorts`
- `addedMergedOutputs`

对 consumer：

- `removedInputPorts`
- `addedMergedInputs`
- `sliceReplacementByOldInputPort`

这样可以先全量算 plan，再统一改图，避免边扫边改把自己状态搞乱。

## 为什么这不会碰顶层信号

因为筛选阶段就直接排除了“与 wrapper top port 有任何关系”的 value。

当前 `repcut` rebuild 时：

- 原始 top 输入会进入 `topValueBySource`
- 原始 top 输出也可能直接复用 link value

所以只要在 pass 里先把：

- `topGraph.inputPorts()`
- `topGraph.outputPorts()`
- `topGraph.inoutPorts()`

涉及到的 value 全部放进一个 `topTouchedValues` 集合，再把这些 value 彻底排除，就不会误伤外部接口。

## 为什么这对当前后端有直接收益

当前后端热路径不是运行时查 hash map。

`emitVerilatorRepCutPackage` 会先把 wrapper graph 展开成 `manifest.connections`，然后按每条 connection 生成静态 C++ scatter/gather 代码，见：

- [`verilator_repcut_package.cpp`](/workspace/wolvrix-playground/wolvrix/lib/emit/verilator_repcut_package.cpp#L1114)
- [`verilator_repcut_package.cpp`](/workspace/wolvrix-playground/wolvrix/lib/emit/verilator_repcut_package.cpp#L1189)

所以现在的瓶颈本质上是：

- 连接数量太多
- 生成出来的逐信号 load/store 语句太多
- 尤其是大量 1bit/窄位宽 link，循环和函数体都被拉长

`repcut-port-merge` 的收益不是“把运行时 hash 换成数组”，而是更上游地减少 edge 数量，让 emit 出来的 scatter/gather 自然变短，并为后续块拷贝、A/B side、批量提交优化打基础。

## 建议的首版范围

首版建议：

- 只处理 `unit_to_unit`
- 支持单 producer，多 sink
- 支持不同位宽成员混合打包
- 不处理 inout
- 不处理与 top port 有关的任何 value
- 不要求最优 merge，只要求稳定且正确

另外，**不建议首版跳过 `debug_part`**。

原因是当前大热点就在 debug scatter；只要某些 link 是 `part_x -> debug_part` 且拓扑完全相同，它们就应该能被同样打包。这仍然满足“不碰顶层信号”的约束。

## 具体实现建议

建议新增：

- [`repcut_port_merge.hpp`](/workspace/wolvrix-playground/wolvrix/include/transform/repcut_port_merge.hpp)
- [`repcut_port_merge.cpp`](/workspace/wolvrix-playground/wolvrix/lib/transform/repcut_port_merge.cpp)

并在 [`transform.cpp`](/workspace/wolvrix-playground/wolvrix/lib/core/transform.cpp) 里注册。

实现上不要做成一个“全图通用 port bundling pass”，而是明确假设输入图就是 repcut rebuild 之后的 wrapper graph。这样可以直接利用：

- `part_*` / `debug_part` 的实例形态
- wrapper 顶层 value 就是 link 的事实
- `moduleName` / `inputPortName` / `outputPortName` 属性都已经齐备

## 实现步骤

建议按下面顺序做：

1. 在 wrapper graph 中提取 `DriverDesc` 和 `SinkDesc`
2. 找出所有纯 `unit_to_unit` 且不触 top 的 link
3. 按 `(producer_instance, sink_instance_set)` 分组
4. 只对 `group.size() >= 2` 的组执行 merge
5. 为每个组计算稳定的 packed layout：`offset/width`
6. 先改 partition subgraph port，再改 wrapper 实例映射
7. 在 wrapper 顶层创建 merged top value，并删除旧细粒度 top value
8. 重建受影响的 instance op
6. 最后跑一次轻量检查：
   - 每个实例的 port 数量与 `inputPortName` / `outputPortName` 对齐
   - 每个 merged input 在 consumer 内都被 slice 完整覆盖
   - wrapper 中不再有悬空旧 link value

## 子图改写顺序建议

为了减少中间态出错，建议顺序固定成：

1. 先扫描 wrapper，形成不可变的 `LinkDesc` / `GroupKey` / `UnitRewritePlan`
2. 再修改各个子图
   - producer 侧创建 `concat + merged output port`
   - consumer 侧创建 `merged input port + slice`
   - consumer 内对旧 input value 做 `replaceAllUses`
   - 最后移除旧 port
3. 修改 wrapper 顶层
   - 创建 merged top value
   - 收集新 instance mapping
   - 新建 instance op
   - 删除旧 instance op
   - 删除旧 top value

这个顺序的好处是：

- 子图先稳定下来，wrapper 只负责重接线
- wrapper 重建时可以直接读取最新的子图 port 列表
- 不需要在修改过程中维护复杂的“旧端口名到新端口名”中间状态

## 命名建议

首版建议直接使用机械但稳定的命名：

- producer merged output port: `pm_out_<group_index>`
- consumer merged input port: `pm_in_<group_index>`
- wrapper merged top value: `repcut_pm_<group_index>`
- producer concat op: `pm_concat_<group_index>`
- consumer slice op: `pm_slice_<group_index>_<member_index>`

不要试图把原成员名拼到 merged 名字里，否则 debug 可读性会提升一点，但名字会很长，且容易在大组里失控。

## 后续可以继续做的优化

有了这个 pass，后面几件事才更值得做：

- 对 merged bus 直接生成块 scatter/gather
- 对 snapshot/writeback 的同拓扑组做批量提交
- 根据 runtime timing 反向调整 merge 粒度和分区权重

也就是说，`repcut-port-merge` 不是最终优化，而是让“数据交换聚合”真正成立的前置条件。
