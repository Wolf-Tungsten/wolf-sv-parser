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
- 截至 `2026-04-12`，replication 之后如果某个 symbol cluster 膨胀超过该上限，`activity-schedule` 会按最终 topo 顺序把它重新切回不超过 `supernode-max-size` 的连续 chunk

也就是说，它限制的是 activity-schedule 内部 partition member count，而不是“最终想要多少个 supernode”。

补充说明：

- 在引入 replication 后置拆分之前，`supernode-max-size` 不能限制 replication 之后的最终 supernode op 数，因此可能出现 `supernode-max-size=72`、但 emit 出单个 `53k op` supernode 的情况
- 引入 replication 后置拆分之后，最终 supernode 的 live op 数也会被重新压回这个上限附近
- 但这仍然不等价于 emit 后文件大小上限；如果某些 op 自身的 C++ 展开非常大，仍可能生成很大的 `sched_*.cpp`

## 当前边界语义

截至 `2026-04-12`，`activity-schedule` 已不再通过 `fixedBoundary` 强制隔离 side-effect op。

当前实现里，`isSideEffectBoundaryKind(...)` 恒为 `false`，原因是：

- GrhSIM 的时序和 side effect 由 event edge / commit 语义控制
- `activity-schedule` 不再额外插入“单次执行保护”
- `kSystemTask` / `kDpicCall` / `kLatchWritePort` 等 op 不再因为 pass 内部边界规则被强制拆开

## replication 后置拆分

截至 `2026-04-12`，`activity-schedule` 在 replication 之后、最终 materialize 之前新增了一步 oversized symbol cluster 拆分：

- 先基于 replication 之后的 graph 重新建立 topo
- 对每个 symbol cluster 收集 live op symbol，并按 topo 顺序排序
- 如果某个 cluster 的 live op 数超过 `supernode-max-size`，就按连续 topo chunk 重新切开

这样做的目的不是改变 DP 的 cost model，而是修复 replication 把单个 supernode 膨胀到远超上限、从而导致极端大 `sched_*.cpp` 的问题。

这个修复对应的代码在 [activity_schedule.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/wolvrix/lib/transform/activity_schedule.cpp) 的 `splitOversizedSymbolClusters(...)`。

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

## 2026-04-12：grhsim-cpp 编译可用性修复

在默认 `supernode-max-size=72`、tie-break 修复已经落地的基础上，这一轮继续处理的是 `grhsim-cpp` 的 C++ 编译体量问题，而不是 supernode 数量本身。

核心修改在 [grhsim_cpp.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/wolvrix/lib/emit/grhsim_cpp.cpp)：

- `ValueStorageClass` 重新整理为四类：`LocalScratch`、`BatchScratch`、`EvalScratch`、`ObjectState`
- 新增 `EvalScratchFrame`，把大量“只需在一次 `eval()` 内存活”的临时值移出 model 对象成员
- `eval_batch_*()` 改为显式接收 `EvalScratchFrame &`
- `refresh_outputs()` 改为显式接收 `const EvalScratchFrame &`
- 宽位逻辑值也不再默认落到对象成员，而是按 word count 放入 `eval_frame.logic_words_N[...]`
- 带 `valueFanout` 的 logic 中间值不再一律物化为 `val__...` 成员，而是尽量放到 `EvalScratch`
- `systemTaskArgExpr()` 改为统一走 `valueRef(...)`，避免 system task 代码路径继续直接引用已迁移走的 `val__...` 字段

这轮修改背后的判断是：

- 编译时间的主瓶颈不是单个 `sched_*.cpp` 太大
- 更大的问题是每个 TU 都要重复解析一个包含海量 `val__...` 成员声明的超大头文件
- 因此正确方向是减少对象成员物化数量，而不是继续机械地把 `.cpp` 切得更碎

### 头文件体量变化

以 XiangShan `SimTop` 为例，这轮修改前后，[grhsim_SimTop.hpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop.hpp) 的体量变化为：

- 早期问题状态：`474556002` bytes，`7453831` 行，`val__` 出现 `5663649` 次
- 仅把部分 scalar logic 迁到 eval scratch 后：`294045391` bytes，`3642140` 行，`val__` 出现 `1976459` 次
- 本轮完成 scalar + wide logic 迁移后：`146340646` bytes，`1232537` 行，`val__` 出现 `13654` 次

也就是说，相比最初问题状态：

- 头文件大小降到约 `30.8%`
- 行数降到约 `16.5%`
- `val__` 成员/引用数量降到约 `0.24%`

### 完整 `emu` 编译结果

这轮在 XiangShan difftest 目录下完成了完整 `emu` 构建：

```bash
NOOP_HOME=/workspace/gaoruihao-dev-gpu/wolvrix-playground/testcase/xiangshan \
make -C testcase/xiangshan/difftest emu \
  BUILD_DIR=/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim \
  GEN_CSRC_DIR=/workspace/gaoruihao-dev-gpu/wolvrix-playground/testcase/xiangshan/build/generated-src \
  NUM_CORES=1 WITH_CHISELDB=0 WITH_CONSTANTIN=0 \
  GRHSIM=1 \
  GRHSIM_MODEL_DIR=/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit
```

结果：

- `7008` 个 `sched_*.cpp` 全部编译通过
- 后续静态库归档与最终链接也成功
- 产物为 [build/xs/grhsim/grhsim-compile/emu](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim-compile/emu)
- `emu` 大小 `660178984` bytes，约 `629.6 MiB`

按产物时间戳估算：

- 首个对象文件时间：`2026-04-12 12:07:51`
- `emu` 完成时间：`2026-04-12 12:23:49`
- 整轮编译耗时约 `15 分 59 秒`

这说明当前这套：

- `supernode-max-size=72`
- DP tie-break 修复
- replication 后置拆分
- `EvalScratchFrame` 值存储分层

已经足以把 XiangShan 的 `grhsim` emit 推进到“可以完整编译出 `emu`”的状态。

## 2026-04-12：emit 端去掉“非必要模板代码”

在继续分析 `grhsim-cpp` 编译时间时，发现除了超大公共头之外，生成代码里还存在大量“非必要模板调用”：

- 窄位 scalar concat / bit-pack helper 被生成为 header-only 模板
- 宽位 `slice/cast/scalar-concat` helper 也通过 `std::array<N>` 模板实例化
- `7008` 个 `sched_*.cpp` 会重复解析并实例化这批 helper

针对这个问题，在 [grhsim_cpp.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/wolvrix/lib/emit/grhsim_cpp.cpp) 中做了两轮 emit 侧收缩：

- 第一轮：把以下 helper 从模板改为普通函数
  - `grhsim_cat`
  - `grhsim_cat_prefix`
  - `grhsim_cat_rhs`
  - `grhsim_pack_bits_u64`
  - `grhsim_concat_scalars_u64`
  - `grhsim_concat_uniform_scalars_u64`
- 第二轮：把以下宽位 helper 的调用路径改为“普通 helper + 局部 `std::array` 包装返回值”
  - `grhsim_cast_words`
  - `grhsim_slice_words`
  - `grhsim_concat_scalars_words`
  - `grhsim_concat_uniform_scalars_words`

静态统计结果：

- 初始状态：`sched` 中显式 `grhsim_foo<...>` 调用共 `466169` 次
- 去掉窄位非必要模板后：降到 `170851`
- 再去掉宽位 `slice/cast/scalar-concat` 显式模板调用后：降到 `0`

对应最热点的显式模板调用依次被清掉：

- `grhsim_slice_words`
- `grhsim_cast_words`
- `grhsim_cat_rhs`
- `grhsim_cat`
- `grhsim_concat_uniform_scalars_u64`
- `grhsim_pack_bits_u64`

需要注意的是：

- 截至这次修改，[grhsim_SimTop_runtime.hpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_runtime.hpp) 中仍然还有 `69` 个模板定义
- 这些剩余模板主要是宽位算术/比较/移位等真正按 `N` 专用化的 helper
- 也就是说，这次改动完成的是“emit 不再生成非必要模板调用”，而不是“runtime.hpp 完全无模板”

这个结论支持了一个更明确的工程方向：

- 非必要 helper 尽量改成普通函数
- 只有确实依赖 `N` 专用化收益的宽位算法，才保留模板实现

## 2026-04-12：`value` / `evt_edge` 改为 typed slot array

在继续分析 `grhsim-cpp` 的头文件体积时，确认当前主要瓶颈不是单一 helper，而是生成类把海量对象按“一个值/一个状态/一个 shadow 一个字段”直接展开到了头文件里。

因此先做了第一轮更激进的存储收敛：

- 所有 `value` 不再生成独立字段
- 改为按数据类型落到少量连续 slot 数组
  - `value_bool_slots_`
  - `value_u8_slots_`
  - `value_u16_slots_`
  - `value_u32_slots_`
  - `value_u64_slots_`
  - `value_words_<N>_slots_`
  - `value_string_slots_`
- `evt_edge` 也不再生成独立字段
  - 改为 `event_edge_slots_[idx]`
- `valueRef(...)` / 事件表达式引用统一改成“数组名 + 常量索引”
- `init()` 中的 `kValues` / `kEventEdges` 也改为整池初始化，不再逐 value / 逐 edge 展开

这一轮没有继续保留 `local/batch/eval/object` 的值存储细分。对于非输入 `value`，emit 侧统一视为可索引 object slot。

对 XiangShan (`xs-default`, `supernode-max-size=72`) 的实测结果：

- [grhsim_SimTop.hpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop.hpp)
  - 修改前：`273,227,744` bytes
  - 修改后：`145,602,254` bytes
- 头文件中的 `val_*` 成员声明：从海量展开降为 `0`
- 头文件中的 `evt_edge_*` 成员声明：降为 `0`
- 只保留每类 slot array 的少量声明，各 `sched_*.cpp` 通过常量索引访问

这个结果说明：

- “全 value 数组化” 对头文件体积的收益是立刻可见的
- 这一步已经消掉了当前头文件中最大的一块膨胀来源
- 剩余的大头主要转移为
  - `state_reg_* / state_latch_* / state_mem_*`
  - `state_shadow_*`
  - `memory write shadow`

功能回归方面，至少以下 spot-check 已重新通过：

- `make run_hdlbits_grhsim DUT=071 SKIP_PY_INSTALL=1`

后续若继续压缩头文件，下一优先级应是：

- `state shadow` 数组化
- `memory write shadow` 数组化
- `register/latch/memory` 本体按类型建池

## 2026-04-12：`state shadow` / `memory write shadow` 继续池化

在完成 `value` / `evt_edge` 数组化后，头文件里剩下最突出的“非 state 本体”膨胀来源是：

- `state shadow_*`
- `memory write shadow_*`

这一轮继续把这两类对象从“逐字段展开”改成按类型分桶的 slot pool：

- `state shadow`
  - `state_shadow_touched_slots_`
  - `state_shadow_{bool,u8,u16,u32,u64}_slots_`
  - `state_shadow_words_<N>_slots_`
- `memory write shadow`
  - `memory_write_touched_slots_`
  - `memory_write_addr_slots_`
  - `memory_write_data_*`
  - `memory_write_mask_*`

同时：

- `StateShadowDecl` / `WriteDecl` 不再保存字段名字符串
- 改为保存 slot index / scalar kind / word count
- `sched` 写 shadow、`init()` 清零 shadow、`commit_state_updates()` 读回 shadow 都统一走 helper 生成的索引表达式

对 XiangShan (`xs-default`, `supernode-max-size=72`) 的继续实测结果：

- [grhsim_SimTop.hpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop.hpp)
  - `value` 池化后：`145,602,254` bytes
  - `state shadow` / `memory write shadow` 也池化后：`47,111,248` bytes

也就是说，相比最初的字段展开版本：

- 初始：`273,227,744` bytes
- 现在：`47,111,248` bytes

头文件体积已经下降到原来的约 `17.2%`。

静态观察上：

- `val_*` 成员声明：`0`
- `evt_edge_*` 成员声明：`0`
- `state shadow` / `memory write shadow` 也不再按对象逐字段展开
- 头文件中剩余的 `state_shadow_` / `memory_write_` 命中主要是
  - 少量 pool 声明
  - `commit_state_shadow_chunk_*` / `commit_write_chunk_*` 方法声明

回归点验：

- `make run_hdlbits_grhsim DUT=071 SKIP_PY_INSTALL=1` 通过
- `make run_hdlbits_grhsim DUT=117 SKIP_PY_INSTALL=1` 通过

做到这一步以后，`grhsim-cpp` 头文件剩余的大头已经非常明确：

- `state_reg_*`
- `state_latch_*`
- `state_mem_*`

如果继续压缩头文件，下一步最值得做的就是把 state 本体也按类型建池。

## 2026-04-12：`state` 本体也改为 typed pool

在前两轮之后，头文件剩余的主要膨胀来源已经只剩：

- `state_reg_*`
- `state_latch_*`
- `state_mem_*`

这一轮继续把 state 本体也从“逐字段对象”改成了 typed pool：

- `register` / `latch`
  - `state_logic_{bool,u8,u16,u32,u64}_slots_`
  - `state_logic_words_<N>_slots_`
- `memory`
  - `state_mem_{u8,u16,u32,u64}_slots_`
  - `state_mem_words_<N>_slots_`

实现上：

- `StateDecl` 不再保存 `fieldName`
- 改为保存
  - `slotIndex`
  - `scalarKind`
  - `wordCount`
- 统一通过 `stateRef(state)` 生成访问表达式
- `register/latch` 直接引用 pool 元素
- `memory` 引用 pool 中的 `vector<elem>` / `vector<array<...>>`
- `init()` 新增独立的 `kStateStorage` 阶段，先分配 state pool，再做逐 state 初始化

对 XiangShan (`xs-default`, `supernode-max-size=72`) 的实测结果继续下降：

- 初始字段展开版本：`273,227,744` bytes
- `value + evt_edge` 池化后：`145,602,254` bytes
- `state shadow + memory write shadow` 池化后：`47,111,248` bytes
- `state` 本体也池化后：
  - [grhsim_SimTop.hpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop.hpp)
  - `411,948` bytes

静态上可见的变化：

- `state_reg_*`：`0`
- `state_latch_*`：`0`
- `state_mem_*` 的命中只剩少量 pool 声明
- 头文件里 state 相关主体已经收敛为几十行 pool 声明，而不再是几十万行成员展开

这意味着头文件体积相比最初版本已经下降到约 `0.15%`。

回归点验：

- `make run_hdlbits_grhsim DUT=071 SKIP_PY_INSTALL=1` 通过
- `make run_hdlbits_grhsim DUT=117 SKIP_PY_INSTALL=1` 通过

做到这一步后，`grhsim-cpp` 的“超大头文件”问题基本已经被结构性解决。
