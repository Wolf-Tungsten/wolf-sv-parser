# activity-schedule

## 功能概述

`activity-schedule` pass 为单个 graph 构建 GrhSIM 用的 activity supernode 调度结果，并把结果写入 session。

它不会把 graph 改写成新的 wrapper/module 结构，但会：

- 为缺失 symbol 的可分区 op 补内部 symbol
- 冻结 graph
- 构建 seed partition
- 做 coarsen
- 做基于 `supernode-max-size` 的 DP 分段和可选 refine
- 做 replication
- 重新 materialize 最终 supernode DAG
- 导出 `supernode_to_ops` / `op_to_supernode` / `value_fanout` / `topo_order` / `state_read_supernodes`

这个 pass 目前主要服务于 `grhsim-cpp` emit。

## 路径语义

`-path` 的解析规则和其他 path-based pass 一致。

- 单段路径：直接按 graph 名选中目标 graph
- 多段路径：`<root>.<inst>...`

多段路径从 root graph 开始，逐层按实例 `instanceName` 查找，并通过实例的 `moduleName` 进入下一级 graph。

## 主要选项

`ActivityScheduleOptions` 定义在 [activity_schedule.hpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/wolvrix/include/transform/activity_schedule.hpp)。

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `-path` | 无 | 目标 graph / 实例路径，必填 |
| `-supernode-max-size` | `72` | DP 分段和 coarsen 的 cluster member 上限 |
| `-enable-coarsen` | `true` | 是否执行 coarsen |
| `-enable-chain-merge` | `true` | 是否启用 out1 / in1 chain merge |
| `-enable-sibling-merge` | `true` | 是否启用 sibling merge |
| `-enable-forward-merge` | `true` | 是否启用 forwarder merge |
| `-enable-refine` | `true` | 是否在 DP 分段后做边界微调 |
| `-refine-max-iter` | `4` | refine 迭代上限 |
| `-enable-replication` | `true` | 是否执行 replication |
| `-replication-max-cost` | `2` | replication 成本上限 |
| `-replication-max-targets` | `8` | 单 op replication 目标上限 |
| `-cost-model` | `edge-cut` | 当前只支持 `edge-cut` |

## Session 输出

对 `path=<target>`，pass 会写入以下 key：

- `<target>.activity_schedule.supernode_to_ops`
- `<target>.activity_schedule.op_to_supernode`
- `<target>.activity_schedule.value_fanout`
- `<target>.activity_schedule.topo_order`
- `<target>.activity_schedule.state_read_supernodes`

这些 key 的写入位置见 [activity_schedule.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/wolvrix/lib/transform/activity_schedule.cpp)。

## `supernode-max-size` 的真实含义

这个参数不是：

- 最终 supernode 数量目标
- 单个 supernode 的 bit 数上限
- 单个 supernode 的输出行数上限
- emit 后文件大小上限

它的实际语义是：

- 在 coarsen 合并时，限制 merged cluster 的 member 数
- 在 DP 分段时，限制 segment 的 member 总数
- refine 也沿用同一个 member 上限

也就是说，它限制的是 activity-schedule 内部 partition member count，而不是“最终想要多少个 supernode”。

## 当前边界语义

截至 `2026-04-12`，`activity-schedule` 已不再通过 `fixedBoundary` 强制隔离 side-effect op。

当前实现里，`isSideEffectBoundaryKind(...)` 恒为 `false`，原因是：

- GrhSIM 的时序和 side effect 由 event edge / commit 语义控制
- `activity-schedule` 不再额外插入“单次执行保护”
- `kSystemTask` / `kDpicCall` / `kLatchWritePort` 等 op 不再因为 pass 内部边界规则被强制拆开

这次调整对应的代码在 [activity_schedule.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/wolvrix/lib/transform/activity_schedule.cpp)。

## XiangShan 实测记录

下面的数字都来自真实运行，不是估算。

测试环境：

- 目标：`SimTop`
- 流程：`make xs_wolf_grhsim_emit`
- 输入：`build/xs/grhsim/wolvrix_xs_post_stats.json`
- 相关脚本：[`scripts/wolvrix_xs_grhsim.py`](/workspace/gaoruihao-dev-gpu/wolvrix-playground/scripts/wolvrix_xs_grhsim.py)

### `supernode-max-size=128`

命令：

```bash
make xs_wolf_grhsim_emit XS_WOLF_GRHSIM_SUPERNODE_MAX_SIZE=128
```

日志：

- [xs_wolf_grhsim_build_20260412_000231.log](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/logs/xs/xs_wolf_grhsim_build_20260412_000231.log)

结果：

- `supernodes=802073`
- `seed_supernodes=5883646`
- `coarse_supernodes=1315150`
- `dp_supernodes=1315119`
- `replication_cloned=6034308`
- `replication_erased=2114916`
- `activity-schedule` 耗时 `233854 ms`
- `write_grhsim_cpp` 耗时 `39649 ms`
- 总耗时 `310251 ms`
- emit 目录总大小 `4340807690` bytes，约 `4.34 GB`
- emit 文件数 `17341`
- 其中 `sched_*.cpp` `17267` 个
- `state_init_*.cpp` `37` 个
- `state_commit_shadow_*.cpp` `30` 个
- `state_commit_write_*.cpp` `1` 个

### `supernode-max-size=75`（tie-break 修复前基线）

命令：

```bash
make xs_wolf_grhsim_emit XS_WOLF_GRHSIM_SUPERNODE_MAX_SIZE=75
```

日志：

- [xs_wolf_grhsim_build_20260412_014220.log](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/logs/xs/xs_wolf_grhsim_build_20260412_014220.log)
- 当时的 `activity_schedule_supernode_stats.json` 已被后续重跑覆盖，分布数据见下文摘录

结果：

- `supernodes=815101`
- `seed_supernodes=5883646`
- `coarse_supernodes=1389863`
- `dp_supernodes=1389863`
- `replication_cloned=6168817`
- `replication_erased=2242731`
- `activity-schedule` 耗时 `197259 ms`
- `write_grhsim_cpp` 耗时 `39350 ms`
- 总耗时 `272782 ms`

最终 `ops / supernode` 分布：

- 平均值 `12.035`
- 中位数 `2`
- `p90=16`
- `p99=137`
- 最大值 `92141`

这组数据说明：

- `supernode-max-size=75` 已经正确传入
- 但它只是 partition member 上限，不是目标装箱大小
- 它也不是最终 `supernode_to_ops[i].size()` 的硬上限
- 在这次运行里，`dp_supernodes` 与 `coarse_supernodes` 完全相同，说明 DP 阶段基本没有继续把 coarse cluster 合并大
- 最终 `ops / supernode` 的 `max=92141` 也进一步说明，replication 和 final materialize 之后，最终 `op` 数已经不再受这个值直接约束
- 因此最终 `ops / supernode` 的平均值仍然远低于 `75`
- 额外静态排查发现，当 `buildDpSegments()` 遇到等成本候选时，旧实现会优先选择 `begin` 更大的方案，也就是偏向更短的末段；这会进一步压低 segment fill ratio
- 该 tie-break 已在 `2026-04-12` 改为“同成本优先更长 segment”，后续 sweep 需要以新结果为准

### `supernode-max-size=75`（tie-break 修复后重跑）

命令：

```bash
make xs_wolf_grhsim_emit XS_WOLF_GRHSIM_SUPERNODE_MAX_SIZE=75
```

日志：

- [xs_wolf_grhsim_build_20260412_020705.log](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/logs/xs/xs_wolf_grhsim_build_20260412_020705.log)
- [activity_schedule_supernode_stats.json](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/activity_schedule_supernode_stats.json)

结果：

- `supernodes=64616`
- `seed_supernodes=5883646`
- `coarse_supernodes=1389863`
- `dp_supernodes=88477`
- `replication_cloned=5663308`
- `replication_erased=2323791`
- `activity-schedule` 耗时 `194666 ms`
- `write_grhsim_cpp` 耗时 `38500 ms`
- 总耗时 `269426 ms`
- emit 目录总大小 `3739475879` bytes，约 `3.74 GB`
- emit 文件数 `16041`

最终 `ops / supernode` 分布：

- 平均值 `142.738`
- 中位数 `75.0`
- `p90=218`
- `p99=1179`
- 最大值 `96887`

和修复前的同参数基线相比：

- `supernodes: 815101 -> 64616`
- `dp_supernodes: 1389863 -> 88477`
- `ops_mean: 12.035 -> 142.738`
- `ops_median: 2 -> 75.0`

这次结果说明：

- 问题核心确实在 `buildDpSegments()` 的 tie-break，而不是 `supernode-max-size` 根本没有传进去
- `coarse_supernodes` 基本不变，说明改动没有改变 coarsen 形状，收益主要来自 DP 真正开始把 coarse cluster 装进更长 segment
- 修复后，`supernode-max-size=75` 已经足以把最终 `supernodes` 压到 `64k` 量级
- 因此此前关于“单调调大 `supernode-max-size` 也难以下到 `9-10 万`”的判断，只适用于 tie-break 修复前的旧实现

### `supernode-max-size=1024`

命令：

```bash
make xs_wolf_grhsim_emit XS_WOLF_GRHSIM_SUPERNODE_MAX_SIZE=1024
```

日志：

- [xs_wolf_grhsim_build_20260412_001132.log](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/logs/xs/xs_wolf_grhsim_build_20260412_001132.log)

结果：

- `supernodes=703585`
- `seed_supernodes=5883646`
- `coarse_supernodes=1049777`
- `dp_supernodes=1002797`
- `replication_cloned=5593152`
- `replication_erased=1690069`
- `activity-schedule` 耗时 `221216 ms`
- `write_grhsim_cpp` 耗时 `38799 ms`
- 总耗时 `296424 ms`
- emit 目录总大小 `4291554856` bytes，约 `4.29 GB`
- emit 文件数 `17170`

### 对比结论

从修复前的 `128` 提高到修复前的 `1024` 后：

- `supernodes` 从 `802073` 降到 `703585`
- 只减少了 `98488`
- 降幅约 `12.3%`

这说明在 tie-break 修复前的旧实现里，当前 XiangShan 规模下：

- `supernode-max-size` 会影响结果
- 但影响不是线性的
- 最终 supernode 数量并不主要受这个参数单独控制
- 最终 `ops / supernode` 的平均值也不会自动逼近这个上限

尤其值得注意的是，修复前旧实现中：

- `75` 时 `coarse_supernodes=1389863`，`dp_supernodes=1389863`
- `128` 时 `dp_supernodes=1315119`
- `1024` 时 `dp_supernodes=1002797`
- 但最终 `supernodes` 仍然有 `703585`

以及：

- `75` 时最终 `ops / supernode` 平均值只有 `12.035`
- 中位数只有 `2`

这说明在修复前旧实现中，真正的瓶颈已经不只是 DP 分段上限，还包括：

- coarsen 的图结构约束
- symbol partition
- replication 之后的最终 materialize 形状

## 对 “90k-100k supernode” 目标的判断

基于 `2026-04-12` 的 XiangShan 实测，可以明确记录：

- 在 tie-break 修复前，不能指望仅通过调大 `supernode-max-size`，把 `supernodes` 从 `70-80 万` 直接压到 `9-10 万`
- 在 tie-break 修复后，`supernode-max-size=75` 已经可以把 `supernodes` 压到 `64616`

因此，后续优化方向应区分两件事：

- 如果目标只是把数量降到 `90k-100k` 量级，当前 tie-break 修复已经足够，甚至已经低于该目标
- 如果目标是继续理解为什么结果会直接降到 `64k`，则应继续排查 replication 和 final materialize 是否又进一步合并了大量活跃区块

如果接下来还要继续做结构优化，仍然应优先排查：

- coarsen 规则是否仍然过于保守
- replication 策略是否产生了过多活跃分裂
- final materialize / symbol partition 是否保留了过多切口

而不是继续单独放大 `supernode-max-size`。

## 相关实现与脚本变更记录

这轮实验还伴随了几项和 GrhSIM 流程相关的实现调整：

- `scripts/wolvrix_xs_grhsim.py` 在 `activity-schedule` 之后不再 `store_json`
- `Makefile` / `scripts/wolvrix_xs_grhsim.py` 已支持通过 `XS_WOLF_GRHSIM_SUPERNODE_MAX_SIZE` / `WOLVRIX_XS_GRHSIM_SUPERNODE_MAX_SIZE` 传参
- `grhsim-cpp` emit 已支持将超大的 state/init/commit 文件拆分为多个 `.cpp`
- emitter 会清理同前缀旧产物，避免重复 emit 到同一路径时残留过期 `sched_*.cpp`

如果后续继续做 sweep，建议至少记录：

- `supernode-max-size`
- `supernodes`
- `coarse_supernodes`
- `dp_supernodes`
- `replication_cloned`
- `replication_erased`
- `activity-schedule` 耗时
- emit 总大小
