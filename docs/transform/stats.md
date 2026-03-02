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

## 输出格式

```
graphs=<n>, operations=<m>, values=<k>
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
graphs=5, operations=1250, values=980

# 优化后
graphs=5, operations=875, values=720
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
- 操作的计数包含所有类型的运算操作
