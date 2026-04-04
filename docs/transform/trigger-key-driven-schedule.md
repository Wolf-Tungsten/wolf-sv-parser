# trigger-key-driven-schedule

## 功能概述

`trigger-key-driven-schedule` pass 为指定路径对应的单个 graph 构建一套静态调度计划，并把结果发布到 scratchpad。

这套计划以 `op` 为最小调度单元，把参与调度的逻辑划分为三类 `TKDGroup`：

- `SinkTKDGroup`
- `TriggerTKDGroup`
- `SimpleTKDGroup`

运行时后续流程可以基于这些 group 的拓扑顺序和 `AffectedSinkSet`，决定本拍真正需要求解哪些逻辑，而不必每次都重算整个 graph。

该 pass 的最终产物不回写成新的 graph 结构。除 top-level observable sink 正规化外，graph 只读，结果统一写入 scratchpad。

## 路径语义

`trigger-key-driven-schedule` 使用 `-path` 指定目标，支持两种形式：

- 单段路径：直接按 graph 名查找
- 多段路径：`<root>.<inst>...`

多段路径的解析规则与 `repcut`、`instance-inline` 一致：

1. 第 1 段是 root graph 名
2. 从第 2 段开始，每一段都按当前 graph 中实例的 `instanceName` 匹配
3. 找到实例后，读取该实例的 `moduleName` 跳转到下一级 graph
4. 最终得到目标 graph

例如：

- `SimTop.logic_part`
- `top.u_core.u_dut`

## 输入约束

本 pass 在单个 `GRH Graph` 上工作。目标 graph 在进入正式构建阶段之前，必须已经完成必要规整。

当前实现要求目标 graph 中不再存在：

- `kInstance`
- `kBlackbox`
- `kXMRRead`
- `kXMRWrite`

也就是说，推荐先完成：

- `xmr-resolve`
- 层次展开或等价规整

此外，这个 pass 的很多结果直接记录 `ValueId` 和 `OperationId`。这些 id 只对当前 graph 的当前结构稳定，因此本 pass 采用“两阶段”约束：

1. 先做必要的 top-level sink 正规化
2. 正规化完成后冻结 graph，并在 frozen graph 上构建 TKD 结果

## 核心概念

### 最小调度单元

本方案以 `op` 为最小调度单元。`value` 只作为依赖边上的数据载体，不单独参与调度。

### sink op

当前实现把以下操作视为 `sink op`：

- storage write sink
  - `kRegisterWritePort`
  - `kMemoryWritePort`
  - `kLatchWritePort`
- effect sink
  - 无返回值 `kDpicCall`
  - `kSystemTask`
- top-level observable sink
  - 其 result 直接绑定到 top-level `output`
  - 其 result 直接绑定到 top-level `inout.out`
  - 其 result 直接绑定到 top-level `inout.oe`

这里的“直接绑定”只按 port binding 判断，不按传递闭包扩张。

### top-level observable sink 正规化

如果某个直接绑定到 top-level `output` / `inout.out` / `inout.oe` 的值，同时还被其它 `op` 使用，pass 会先在该值与 top-level port 之间插入一个专用 `kAssign`。

正规化之后：

- 真正进入 `SinkTKDGroup` 的是这个专用 `kAssign`
- 原来的组合 `op` 仍按普通 use-def 关系参与后续分组

这一步只发生在步骤 1 之前。

### TriggerKey

每个事件敏感的 `sink op` 都会提取一个 `TriggerKey`。

`TriggerKey` 有两种形态：

- 空键
- 非空键：由若干 `(valueId, eventEdge)` 事件项组成

规范化规则是：

1. 展开成若干 `(valueId, eventEdge)` 二元组
2. 先按 `valueId` 排序
3. 对相同 `valueId`，再按 `eventEdge` 排序，固定 `negedge < posedge`
4. 对完全相同的二元组去重

两个 `TriggerKey` 相等，当且仅当规范化后的事件项序列完全相同。

### TKDGroup

`TKDGroup` 是运行时直接调度的 group。当前实现包含三类：

- `SinkTKDGroup`
  - 由 `sink op` 按 `TriggerKey` 分组得到
- `TriggerTKDGroup`
  - 当前实现只有一个全局 `TriggerTKDGroup`
  - 收集所有非空 `TriggerKey` 中事件 value 的上游驱动逻辑
- `SimpleTKDGroup`
  - 从 `SinkTKDGroup` 逆向传播 `AffectedSinkSet`
  - 按相同 `AffectedSinkSet` 把剩余普通 `op` 分组

基础约束：

- 任意两个 `TKDGroup` 不相交
- 任意一个 `op` 至多属于一个 `TKDGroup`

并不是所有 `op` 都会进入 `TKDGroup`。当前实现会排除：

- `kRegister`
- `kMemory`
- `kLatch`
- `kDpicImport`

## 构建流程

### 步骤 1：收集 sink op 并构建 `SinkTKDGroup`

pass 会先完成 top-level observable sink 正规化，然后重新扫描 graph：

- 收集全部 intrinsic sink
- 收集全部 top-level observable sink
- 为每个 sink 提取并规范化 `TriggerKey`
- 按 `TriggerKey` 分桶，得到 `SinkTKDGroup`

每个 `SinkTKDGroup` 自带一个单元素的 `AffectedSinkSet`，即只包含自己。

### 步骤 2：构建全局 `TriggerTKDGroup`

pass 会收集所有非空 `TriggerKey` 的事件 value，按 `ValueId` 去重后作为 trigger roots。

然后从这些 root 沿 use-def 链逆向回溯：

- 收集驱动这些事件 value 的普通逻辑
- 跳过已经属于 `SinkTKDGroup` 的 `op`
- 跳过声明类 `op`

最终得到一个全局 `TriggerTKDGroup`。

这个 group 的 `AffectedSinkSet` 等于“全部非空 `TriggerKey` 对应的 `SinkTKDGroup` 集合”。

### 步骤 3：构建 `SimpleTKDGroup`

pass 从每个 `SinkTKDGroup` 的成员 `op` 出发，沿 operands 的定义链逆向传播 `AffectedSinkSet`：

- 如果一个上游 `op` 最终只影响某一个 sink group，它会落到对应的单集合 `SimpleTKDGroup`
- 如果一个上游 `op` 同时影响多个 sink group，它会落到这些 sink group 的并集对应的 `SimpleTKDGroup`
- 如果某个可执行 `op` 不影响任何 sink，它会落到空集合对应的 `SimpleTKDGroup`

已进入 `SinkTKDGroup` 或 `TriggerTKDGroup` 的 `op` 不会再被放进 `SimpleTKDGroup`。

### 步骤 4：构建 group 级依赖并拓扑排序

pass 会把 `op -> op` 数据依赖提升成 `TKDGroup -> TKDGroup` 边：

- 若某个 group 的 result 被另一个 group 的 `op` 使用，则建立一条 data dependency 边
- 重边会去重

另外，当前实现会额外加入：

- `TriggerTKDGroup -> 所有其它 TKDGroup`

这样后续流程在使用 toposort 结果时，可以保证 trigger 相关逻辑被视为全局前置阶段。

最后使用 `toposort` 组件对全部 `TKDGroup` 做拓扑排序，产出最终执行顺序。

## Scratchpad 产物

本 pass 的结果统一发布到：

```text
tkds/<modulePath>/...
```

当前发布的 key 包括：

```text
tkds/<modulePath>/meta
tkds/<modulePath>/pools/triggerKeys
tkds/<modulePath>/pools/affectedSinkSets
tkds/<modulePath>/groups/sink
tkds/<modulePath>/groups/trigger
tkds/<modulePath>/groups/simple
tkds/<modulePath>/index/opToTkdGroup
tkds/<modulePath>/plan/edges
tkds/<modulePath>/plan/topoOrder
```

各条目的含义：

- `meta`
  - schema version、modulePath、graph symbol、计数信息、正规化统计
- `pools/triggerKeys`
  - `TriggerKey` intern 池
- `pools/affectedSinkSets`
  - `AffectedSinkSet` intern 池
- `groups/sink`
  - 全部 `SinkTKDGroup` 记录
- `groups/trigger`
  - 全局 `TriggerTKDGroup` 记录
- `groups/simple`
  - 全部 `SimpleTKDGroup` 记录
- `index/opToTkdGroup`
  - `OperationId.index -> TkdGroupId` 稠密索引
- `plan/edges`
  - `TKDGroup` 级依赖边
- `plan/topoOrder`
  - `TKDGroup` 拓扑顺序

中间 worklist、visited 标记、临时分桶不会写入 scratchpad。

## 主要选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-path` | 无 | 目标 graph 路径，必填 |

## 示例

### 示例 1：直接对某个 graph 建立 TKD 调度计划

```bash
wolvrix --pass=trigger-key-driven-schedule:-path=top input.json
```

### 示例 2：对层级中的单个子模块建立 TKD 调度计划

```bash
wolvrix --pass=trigger-key-driven-schedule:-path=SimTop.logic_part input.json
```

## 注意事项

- 多段路径按实例名解析，不按模块名解析
- 该 pass 当前只处理单个目标 graph，不会递归处理内部实例指向的子图
- top-level observable sink 只看“直接绑定”，不会把整个上游组合锥提前并入 sink
- 当前实现固定只构建一个全局 `TriggerTKDGroup`
- 若 graph 仍保留层次结构或 XMR，pass 会直接报错
- 最终结果通过 scratchpad 向后续流程发布，不会重写成新的 graph 结构
