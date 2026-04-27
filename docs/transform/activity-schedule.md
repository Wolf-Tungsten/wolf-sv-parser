# activity-schedule

## 功能概述

`activity-schedule` pass 为单个 graph 构建 GrhSIM 用的 activity supernode 调度结果，并把结果写入 session。

它不会把 graph 改写成新的 wrapper/module 结构，但会：

- 为缺失 symbol 的可分区 op 补内部 symbol
- 冻结 graph
- 先构造 `sink-supernode`
- 构建 `sink + residual singleton` seed partition
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
| `-max-sink-supernode-op` | `4096` | `sink-supernode` 的 topo chunk 上限 |
| `-enable-coarsen` | `true` | 是否执行 coarsen |
| `-enable-chain-merge` | `true` | 是否启用 out1 / in1 chain merge |
| `-enable-sibling-merge` | `true` | 是否启用 sibling merge |
| `-enable-forward-merge` | `true` | 是否启用 change-coupled singleton merge，并保留 legacy alias/forwarder merge fallback |
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

## 当前 special partition 语义

截至 `2026-04-27`，`activity-schedule` 的 special partition / seed 构造分两步：

1. 先把 `sink op` 按 topo 顺序切成 `sink-supernode`
2. 再把不在 `sink-supernode` 中的 residual op 直接保留为 singleton seed，交给后续 `coarsen`

当前 `sink-supernode` 的语义是：

- 只包含本地 state write：
  - `kRegisterWritePort`
  - `kLatchWritePort`
  - `kMemoryWritePort`
- `kSystemTask` / `kDpicCall` 不再进入 `sink-supernode`

也就是说，`activity-schedule` 当前不再构造 `tail-supernode`；residual 图的主聚合全部交给 `coarsen`。

## 当前 `coarsen` 的 change-coupled 规则

截至 `2026-04-27`，`enable-forward-merge` 已不再只表示“forwarder merge”。

当前它会优先尝试把 singleton cluster 合并到某个前驱 cluster，但前提是：这个 op 的输出对该前驱输入是“输入一变，输出一定变”的关系。

目前已经显式纳入的规则是：

- `kNot`
- `kAssign`，但只在结果位宽不小于输入位宽时成立，也就是不能靠截断吞掉输入变化
- `kConcat`
- `kReplicate` 的 data operand
- `kAdd` / `kSub` / `kXor` / `kXnor`，但只在“唯一动态前驱 + 其余 operand 都是常量”的情况下成立

当前明确不纳入这类 guaranteed-change merge 的 op 包括：

- `kSliceStatic` / `kSliceDynamic`：输入变化可能落在未被观察的 slice 之外
- `kMux`：变化可能发生在未选中的数据支路
- `kEq` / `kNe` / 比较类 / reduction 类：多个不同输入可能映射到同一个输出
- `kAnd` / `kOr` / `kMul` / shift 类：输入变化可能被 mask、折叠或截断掉

如果 guaranteed-change 规则不命中，当前实现仍会保留一层 legacy alias/forwarder fallback，主要覆盖：

- `kAssign`
- `kConcat`
- `kSliceStatic`
- `kSliceDynamic`

这意味着当前版本已经开始把 `coarsen` 往“同步变动驱动”的方向收敛，但还没有完全删掉旧的 forwarder 兼容路径。

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

补充说明：

- 它不限制 `sink-supernode`
- 因此最终 `supernode_to_ops[i].size()` 可能明显大于 `supernode-max-size`
- 这仍然不等价于 emit 后文件大小上限；如果某些 op 自身的 C++ 展开非常大，仍可能生成很大的 `sched_*.cpp`

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

后续在继续定位 `grhsim-cpp` 运行语义问题时，这套 `ValueStorageClass` / `EvalScratchFrame` 分层又被整体撤回了。当前实现重新回到更直接的模型：

- 所有非输入 value 统一分配到持久 `value_*_slots_`
- `eval_batch_*()` / `refresh_outputs()` 不再传 `eval_frame`
- 先消除 emitter 自身的分类分叉，再单独排查 `grhsim-cpp` 的调度与执行语义

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

## 2026-04-13：单线程 `grhsim` / XiangShan coremark 早期失败排查

本节只记录单线程 `grhsim` 的排查结果，与 `repcut` 无关。

### 当前稳定复现点

直接运行当前增量链接后的 `emu`：

```bash
cd /workspace/gaoruihao-dev-gpu/wolvrix-playground
./build/xs/grhsim/grhsim-compile/emu \
  -i ./testcase/xiangshan/ready-to-run/coremark-2-iteration.bin \
  --diff ./testcase/xiangshan/ready-to-run/riscv64-nemu-interpreter-so \
  -b 0 -e 0
```

稳定复现结果：

- `instrCnt = 2`
- `cycleCnt = 662`
- 首先命中的断言包括：
  - [MEFreeList.sv](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/rtl/rtl/MEFreeList.sv:2026)
  - [StdFreeList.sv](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/rtl/rtl/StdFreeList.sv:1381)
  - [StdFreeList_1.sv](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/rtl/rtl/StdFreeList_1.sv:652)

这三个断言里，当前最先拿到有效运行时证据的是 `MEFreeList.sv:2026`。

### 参考波形对拍结论

参考波形文件：

- [tmp/xs_wolf_20260413_165458.fst](/workspace/gaoruihao-dev-gpu/wolvrix-playground/tmp/xs_wolf_20260413_165458.fst)

波形工具验证可用：

- [fst_tree.py](/workspace/gaoruihao-dev-gpu/wolvrix-playground/tools/fst_tools/fst_tree.py)
- [fst_cycle_trace.py](/workspace/gaoruihao-dev-gpu/wolvrix-playground/tools/fst_tools/fst_cycle_trace.py)

在参考波形中，`intFreeList` 就是 RTL 中 `MEFreeList intFreeList (...)` 的实例。抽取 `cycle 656..662` 后，得到：

- `headPtrOH[223:0]` 在 `cycle 657..662` 一直是 bit `2`
- `headPtr_flag = 0`
- `headPtr_value = 2`
- `debugArchHeadPtr_flag = 0`
- `debugArchHeadPtr_value = 1`
- `debugArchHeadPtr_REG_flag = 0`
- `debugArchHeadPtr_REG_value = 1`
- `freeRegCntReg = 0xdd`
- `io_doAllocate = 1`
- `io_allocateReq_0..7 = 0,0,0,0,0,0,0,0`

也就是说，参考侧在失败窗口内的行为非常稳定：

- `headPtrOH` 明确是 bit `2`
- `headPtr_value` 也明确是 `2`

### `grhsim-cpp` 插桩结果

为了避免 `make xs_wolf_grhsim_emu` 重跑 emit 覆盖手工修改，当前排查采用：

- 直接修改生成文件：
  - [grhsim_SimTop_sched_1470.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_1470.cpp)
  - [grhsim_SimTop_sched_4336.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_4336.cpp)
  - [grhsim_SimTop_sched_4475.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_4475.cpp)
- 只增量重编命中的 `sched_*.o`
- 直接运行现成的 [emu](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim-compile/emu)

在 `MEFreeList.sv:2026` 断言前拿到的第一条有效插桩是：

```text
[grhsim-assert] eval=1431 file=build/xs/rtl/rtl/MEFreeList.sv line=2026 clock=1 reset=0 cond=1 headptr_neq=1 lhs_words=[0x4,0x0,0x0,0x0] rhs_words=[0x400000000,0x0,0x0,0x0] stop_cond=1
```

它对应的事实是：

- `lhs_words = 0x4`，也就是 bit `2`
- `rhs_words = 0x400000000`，也就是 bit `34`

### 当前有效结论

把参考波形和 `grhsim` 插桩对在一起后，可以得到当前最强结论：

- `grhsim` 在 `MEFreeList.sv:2026` 比较的左侧 one-hot，和参考波形是一致的
  - 都是 bit `2`
- `grhsim` 在同一比较的右侧 one-hot，已经偏成 bit `34`
  - 这与参考波形明显不一致

因此当前问题已经明确收敛为：

- 不是“参考值不确定”
- 不是“最后断言自己算错了”
- 而是 `MEFreeList.sv:2026` 比较参与项中的一侧 one-hot cone，在 `grhsim-cpp` 运行时已经偏离到 bit `34`

### 说明

这一节已经移除此前的推测性判断，只保留当前由运行时对拍直接支持的结论。原因很简单：

- `sv -> sv` 路径已证明正常
- `post_stats` 和 `xs_wolf.json` 中同类编码可以成功 `read_json -> emit_sv`
- 当前最强证据来自运行时对拍：
  - 参考波形右值对应 bit `2`
  - `grhsim` 断言现场右值已经偏到 bit `34`

### 后续排查方向

当前最值得继续追的是：

- `MEFreeList.sv:2026` 比较右侧 `rhs_words=[0x400000000,...]` 的生成链
- 为什么这条 one-hot cone 在 `grhsim-cpp` 运行时会跑到 bit `34`
- 同时继续利用参考波形做逐周期比对，并沿比较右侧 one-hot 的生成链继续收敛

### 2026-04-13 追加：`rhs` 上游同拍瞬时错写链

继续沿 `rhs` 上游做生成文件插桩后，已经把这次单线程 `grhsim` 早期失败收敛到一条明确的“同拍瞬时错写”链上。

这轮新增插桩主要加在：

- [grhsim_SimTop_sched_2425.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_2425.cpp)
- [grhsim_SimTop_sched_1571.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_1571.cpp)
- [grhsim_SimTop_sched_226.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_226.cpp)
- [grhsim_SimTop_sched_5835.cpp](/workspace/gaoruihao-dev-gpu/wolvrix-playground/build/xs/grhsim/grhsim_emit/grhsim_SimTop_sched_5835.cpp)

关键事实：

- `redirectGen$s1_redirect_valid_reg_last_REG` 的本地 mux 链在坏窗口里并不是“稳定算成 1”
- 相反，`sched_2425` 在同一个 `eval` 内会被重复触发
- 在 `eval=1423`，同一拍里先后出现了两次不同结果：
  - 第一轮：`src5198672=1`，于是 `_op_4801683` 算出 `next1693210=1`
  - 第一轮同时触发 `_op_4801686`，在 `posedge` 上把这个瞬时 `1` 写进 `state_shadow_bool_slots_[35953]`
  - 第二轮：`src5198672` 已经回到 `0`，`_op_4801683` 也回到 `0`
- 但第二轮已经不能再覆盖第一轮写入，因为对应 `event_edge` 只在第一次命中 `posedge`

这解释了为什么：

- `eval=1424/1425` 时
  - `value_bool_slots_[1693210]` 已经回到 `0`
  - 但 `value_bool_slots_[1693161] / state_logic_bool_slots_[36960]` 仍然变成了 `1`

沿 `rhs` 继续往上追，当前已经确认这条瞬时 `1` 的传播链为：

1. `state_logic_bool_slots_[42939]`
2. `value_bool_slots_[626213]`
3. `value_bool_slots_[6879360]`
4. `value_bool_slots_[5403883]`
5. `value_bool_slots_[5198672]`
6. `value_bool_slots_[5022462]`
7. `value_bool_slots_[1693210]`
8. `state_logic_bool_slots_[36960]`

其中本轮已经排除的分支：

- `value_bool_slots_[5536218] -> value_bool_slots_[5403876]`
  - 在坏窗口里始终是 `0`
- `value_bool_slots_[5687895]`
  - 在坏窗口里始终是 `1`

因此当前新的最强收敛点是：

- 真正造成瞬时 `1` 的不是 `5403876` 支路
- 而是 `6879360` 在同拍内先出现 `1`、随后又回到 `0`
- `6879360` 又只是 `value_bool_slots_[626213]` 的边界复制
- `626213` 则来自 `state_logic_bool_slots_[42939]`

也就是说，当前最值得继续追的源头已经不再是 `redirectGen` 本地组合 mux 本身，而是：

- 谁在更上游先把 `state_logic_bool_slots_[42939]` 错写成了 `1`
- 以及为什么这次错写会在同一个 `eval` 内先传播到 `6879360`，再被后续计算改回，但已经来不及撤销 `posedge` 写入
