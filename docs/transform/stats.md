# stats

## 功能概述

`stats` pass 统计设计中的图（Graph）、操作（Operation）和值（Value）数量，用于诊断和分析设计规模。

## 详细说明

该 pass 遍历整个设计，收集以下统计信息：

| 指标 | 说明 |
|------|------|
| `graph_count` | 模块/图的数量 |
| `operation_count` | 运算操作的总数 |
| `value_count` | 值（信号/变量）的总数 |
| `value_bitwidth_total` | 值的位宽总和（仅统计 `width > 0` 的值） |
| `value_widths` | 值位宽分布（`{ "<width>": <count> }`） |
| `operation_kinds` | 操作类型分布（`{ "<OperationKind>": <count> }`） |
| `register_widths` | kRegister 宽度分布（`{ "<width>": <count> }`） |
| `latch_widths` | kLatch 宽度分布（`{ "<width>": <count> }`） |
| `memory_widths` | kMemory 宽度分布（`{ "<width>": <count> }`） |
| `memory_capacity_bits` | kMemory 容量分布（`width * row` 的总位数，`{ "<capacity_bits>": <count> }`） |
| `writeport_cone_depths` | 写端口逻辑锥深度分布（更新条件/下一值的组合锥，按 op 最长路径） |
| `writeport_cone_sizes` | 写端口逻辑锥大小分布（锥内组合 op 总数） |
| `writeport_cone_fanins` | 写端口逻辑锥 fan-in 分布（锥内叶子输入 value 数量） |
| `comb_result_user_counts` | 组合逻辑 op 的每个 result value 的直接 user 数分布（仅统计 `Value::users()`，不含输出端口/声明符号） |
| `comb_op_fanout_sinks` | 组合 op 最终驱动的非组合 sink（写端口/系统任务/DPI/实例等）与输出/声明端口数量分布 |
| `readport_fanout_sinks` | k*ReadPort 最终驱动的非组合 sink 与输出/声明端口数量分布 |

## 输出格式

```
{
  "graph_count": <n>,
  "operation_count": <m>,
  "value_count": <k>,
  "value_bitwidth_total": <t>,
  "value_widths": { "<width>": <count> },
  "operation_kinds": { "<OperationKind>": <count> },
  "register_widths": { "<width>": <count> },
  "latch_widths": { "<width>": <count> },
  "memory_widths": { "<width>": <count> },
  "memory_capacity_bits": { "<capacity_bits>": <count> },
  "writeport_cone_depths": { "<depth>": <count> },
  "writeport_cone_sizes": { "<size>": <count> },
  "writeport_cone_fanins": { "<fanin>": <count> },
  "comb_result_user_counts": { "<users>": <count> },
  "comb_op_fanout_sinks": { "<fanout>": <count> },
  "readport_fanout_sinks": { "<fanout>": <count> }
}
```

## 使用示例

```bash
# 基本使用
wolvrix --pass=stats input.sv

# 在优化前后对比
wolvrix --pass=stats --pass=simplify --pass=stats input.sv
```

## 典型输出

```
# 优化前
{"graph_count":5,"operation_count":1250,"value_count":980,"value_bitwidth_total":24016,"value_widths":{"1":520,"8":200,"32":260},"operation_kinds":{"kAssign":200,"kAdd":120},"register_widths":{"32":180},"latch_widths":{"1":40},"memory_widths":{"64":10},"memory_capacity_bits":{"2048":6,"4096":4},"writeport_cone_depths":{"2":18,"3":7},"writeport_cone_sizes":{"4":12,"5":13},"writeport_cone_fanins":{"2":10,"3":15},"comb_result_user_counts":{"0":90,"1":260,"2":40},"comb_op_fanout_sinks":{"0":240,"1":30,"2":5},"readport_fanout_sinks":{"1":80,"2":10}}

# 优化后
{"graph_count":5,"operation_count":875,"value_count":720,"value_bitwidth_total":18040,"value_widths":{"1":380,"8":160,"32":180},"operation_kinds":{"kAssign":160,"kAdd":80},"register_widths":{"32":120},"latch_widths":{"1":20},"memory_widths":{"64":8},"memory_capacity_bits":{"2048":5,"4096":3},"writeport_cone_depths":{"2":12,"3":5},"writeport_cone_sizes":{"3":9,"4":8},"writeport_cone_fanins":{"2":7,"3":10},"comb_result_user_counts":{"0":72,"1":188,"2":19},"comb_op_fanout_sinks":{"0":180,"1":24,"2":4},"readport_fanout_sinks":{"1":60,"2":6}}
```

## 使用场景

- **设计规模评估**：了解设计的复杂度
- **优化效果验证**：对比优化前后的规模变化
- **性能分析**：识别需要进一步优化的模块
- **回归测试**：检测意外的设计规模变化

## 注意事项

- 该 pass 不会修改 IR
- 统计包含所有模块，包括未实例化的模块
- 值的计数包含输入、输出、内部信号等所有值
- 位宽分布中会包含 `width <= 0` 的条目（如果存在）
- 操作的计数包含所有类型的运算操作
- 写端口逻辑锥统计基于 kRegisterWritePort/kLatchWritePort/kMemoryWritePort 的 updateCond 与 nextValue（memory 为 data）
- `comb_result_user_counts` 统计的是组合 op 结果值的直接 op users，不包含输出端口、声明符号等非 op sink
- 组合逻辑 fan-out 统计包含所有非组合 op（如写端口、system task、DPI call、instance/blackbox 等）与输出/声明端口
