# stats

## 功能概述

`stats` pass 统计设计中的图（Graph）、操作（Operation）和值（Value）数量，用于诊断和分析设计规模。

## 详细说明

该 pass 遍历整个设计，收集以下统计信息：

| 指标 | 说明 |
|------|------|
| `graphs` | 模块/图的数量 |
| `operations` | 运算操作的总数 |
| `values` | 值（信号/变量）的总数 |
| `value-bitwidth-total` | 值的位宽总和（仅统计 `width > 0` 的值） |
| `value-widths-json` | 值位宽分布（`{ "<width>": <count> }`） |
| `op-kinds-json` | 操作类型分布（`{ "<OperationKind>": <count> }`） |
| `kRegister-widths-json` | kRegister 宽度分布（`{ "<width>": <count> }`） |
| `kLatch-widths-json` | kLatch 宽度分布（`{ "<width>": <count> }`） |
| `kMemory-widths-json` | kMemory 宽度分布（`{ "<width>": <count> }`） |
| `kMemory-capacity-json` | kMemory 容量分布（`width * row` 的总位数，`{ "<capacity_bits>": <count> }`） |
| `writeport-cone-depths-json` | 写端口逻辑锥深度分布（更新条件/下一值的组合锥，按 op 最长路径） |
| `writeport-cone-sizes-json` | 写端口逻辑锥大小分布（锥内组合 op 总数） |
| `writeport-cone-fanins-json` | 写端口逻辑锥 fan-in 分布（锥内叶子输入 value 数量） |
| `comb-op-fanout-sinks-json` | 组合 op 最终驱动的非组合 sink（写端口/系统任务/DPI/实例等）与输出/声明端口数量分布 |
| `readport-fanout-sinks-json` | k*ReadPort 最终驱动的非组合 sink 与输出/声明端口数量分布 |

## 输出格式

```
stats
  graphs: <n>
  operations: <m>
  values: <k>
  value-bitwidth-total: <t>
  value-widths-json: { "<width>": <count> }
  op-kinds-json: { "<OperationKind>": <count> }
  kRegister-widths-json: { "<width>": <count> }
  kLatch-widths-json: { "<width>": <count> }
  kMemory-widths-json: { "<width>": <count> }
  kMemory-capacity-json: { "<capacity_bits>": <count> }
  writeport-cone-depths-json: { "<depth>": <count> }
  writeport-cone-sizes-json: { "<size>": <count> }
  writeport-cone-fanins-json: { "<fanin>": <count> }
  comb-op-fanout-sinks-json: { "<fanout>": <count> }
  readport-fanout-sinks-json: { "<fanout>": <count> }
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
stats
  graphs: 5
  operations: 1250
  values: 980
  value-bitwidth-total: 24016
  value-widths-json: {"1":520,"8":200,"32":260}
  op-kinds-json: {"kAssign":200,"kAdd":120}
  kRegister-widths-json: {"32":180}
  kLatch-widths-json: {"1":40}
  kMemory-widths-json: {"64":10}
  kMemory-capacity-json: {"2048":6,"4096":4}
  writeport-cone-depths-json: {"2":18,"3":7}
  writeport-cone-sizes-json: {"4":12,"5":13}
  writeport-cone-fanins-json: {"2":10,"3":15}
  comb-op-fanout-sinks-json: {"0":240,"1":30,"2":5}
  readport-fanout-sinks-json: {"1":80,"2":10}

# 优化后
stats
  graphs: 5
  operations: 875
  values: 720
  value-bitwidth-total: 18040
  value-widths-json: {"1":380,"8":160,"32":180}
  op-kinds-json: {"kAssign":160,"kAdd":80}
  kRegister-widths-json: {"32":120}
  kLatch-widths-json: {"1":20}
  kMemory-widths-json: {"64":8}
  kMemory-capacity-json: {"2048":5,"4096":3}
  writeport-cone-depths-json: {"2":12,"3":5}
  writeport-cone-sizes-json: {"3":9,"4":8}
  writeport-cone-fanins-json: {"2":7,"3":10}
  comb-op-fanout-sinks-json: {"0":180,"1":24,"2":4}
  readport-fanout-sinks-json: {"1":60,"2":6}
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
- 组合逻辑 fan-out 统计包含所有非组合 op（如写端口、system task、DPI call、instance/blackbox 等）与输出/声明端口
