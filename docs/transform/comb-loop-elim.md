# comb-loop-elim

## 功能概述

`comb-loop-elim` pass 检测并处理组合逻辑循环（combinational loops），支持识别和修复虚假循环（false loops）。

## 详细说明

组合逻辑循环是指由组合逻辑操作构成的循环依赖关系，这通常是不期望的设计问题。该 pass 使用 Tarjan 强连通分量（SCC）算法来检测循环，并能够区分真实循环和虚假循环。

### 检测方法

1. **构建依赖图**：从每个图的值（Value）构建后继依赖关系图
2. **SCC 检测**：使用 Tarjan 算法找出所有强连通分量
3. **边界操作识别**：将常量、寄存器读端口、锁存器读端口和 DPI-C 调用视为边界，不参与循环检测

### 虚假循环修复

对于检测到的循环，pass 会进一步分析：

1. **位范围分析**：构建基于位范围的细粒度依赖图
2. **切片分析**：分析静态切片（`kSliceStatic`）、动态切片（`kSliceDynamic`）和连接操作（`kConcat`）
3. **冲突检测**：检查同一位范围是否存在重叠的写操作

如果循环中的所有 SCC 都不存在位范围重叠，则判定为虚假循环，可以通过值拆分来修复。

## 配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-max-analysis-nodes` / `-max-nodes` | 0（无限制） | 每个图最大分析的节点数 |
| `-num-threads` / `-threads` | 0（使用硬件并发数） | 分析线程数 |
| `-fix-false-loops` / `-no-fix-false-loops` | true | 是否修复虚假循环 |
| `-max-fix-iter` | 100 | 修复虚假循环的最大迭代次数 |
| `-fail-on-true-loop` | false | 将真实循环视为 pass 失败 |

## 使用示例

```bash
# 基本使用
wolvrix --pass=comb-loop-elim input.sv

# 启用多线程分析
wolvrix --pass=comb-loop-elim:-num-threads=4 input.sv

# 限制分析节点数
wolvrix --pass=comb-loop-elim:-max-analysis-nodes=10000 input.sv

# 不修复虚假循环，仅检测
wolvrix --pass=comb-loop-elim:-no-fix-false-loops input.sv
```

## 输出信息

- 检测到的循环数量和类型（真实循环/虚假循环）
- 循环涉及的值和操作
- 源代码位置信息（如果可用）
- 修复操作的统计信息

## 注意事项

- 大规模设计可能需要调整 `-max-analysis-nodes` 以平衡精度和性能
- 虚假循环修复会修改 IR，可能影响后续的时序分析
- 真实循环通常表明设计存在需要修复的问题
