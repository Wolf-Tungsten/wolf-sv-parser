# Wolvrix Transform Passes

本文档目录包含 Wolvrix 中所有可用的 transform passes 的详细说明。

## 概述

Transform passes 用于对 GRH（Graph RTL Hierarchy）中间表示进行各种优化和转换。每个 pass 执行特定的功能，可以单独使用或组合使用。

## 可用 Passes

### 主要 Passes（可直接使用）

| Pass | 描述 | 典型使用场景 |
|------|------|-------------|
| [`blackbox-guard`](blackbox-guard.md) | 为未解析的黑盒模块创建 stub 图 | 处理部分实现的设计 |
| [`comb-loop-elim`](comb-loop-elim.md) | 检测并修复组合逻辑循环 | 确保设计的可综合性 |
| [`hier-flatten`](hier-flatten.md) | 层次结构扁平化 | 综合优化、形式验证 |
| [`instance-inline`](instance-inline.md) | 按路径内联单个实例 | 局部展开、分区后回填 |
| [`latch-transparent-read`](latch-transparent-read.md) | 建模锁存器透明读行为 | 锁存器设计处理 |
| [`slice-index-const`](slice-index-const.md) | 常量索引动态切片转静态切片 | 切片优化 |
| [`multidriven-guard`](multidriven-guard.md) | 检测多驱动冲突 | 扁平化前的检查 |
| [`strip-debug`](strip-debug.md) | 按路径拆分 debug / logic 子图 | DPI / system task 隔离 |
| [`trigger-key-driven-schedule`](trigger-key-driven-schedule.md) | 为单个 graph 构建 TKD 静态调度计划 | 运行时按需求解、事件驱动调度 |
| [`xmr-resolve`](xmr-resolve.md) | 解析跨模块引用 | 处理 XMR 路径 |
| [`memory-init-check`](memory-init-check.md) | 验证存储器初始化一致性 | 存储器合并检查 |
| [`repcut`](repcut.md) | 按路径对单个模块做 RepCut 分区 | 大模块切分、后续 inline |
| [`simplify`](simplify.md) | 综合优化（常量折叠、冗余消除、死代码消除） | 一般优化 |
| [`stats`](stats.md) | 统计设计规模 | 设计分析 |

### 内部 Passes（由 simplify 调用）

| Pass | 描述 |
|------|------|
| [`const-fold`](const-fold.md) | 常量折叠 |
| [`dead-code-elim`](dead-code-elim.md) | 死代码消除 |
| [`redundant-elim`](redundant-elim.md) | 冗余消除 |

## 使用方式

### 命令行使用

```bash
# 单个 pass
wolvrix --pass=<pass-name> input.sv

# 带选项的 pass
wolvrix --pass=<pass-name>:-<option>=<value> input.sv

# 多个 passes
wolvrix --pass=<pass1> --pass=<pass2> input.sv

# 完整流程示例
wolvrix \
  --pass=blackbox-guard \
  --pass=multidriven-guard \
  --pass=xmr-resolve \
  --pass=hier-flatten \
  --pass=simplify \
  input.sv
```

### C++ API 使用

```cpp
#include "core/transform.hpp"
#include "transform/simplify.hpp"

using namespace wolvrix::lib::transform;

PassManager pm;
pm.addPass(std::make_unique<SimplifyPass>());
auto result = pm.run(design, diags);
```

## Pass 执行顺序建议

### 预处理阶段
1. `blackbox-guard` - 处理黑盒模块
2. `memory-init-check` - 验证存储器初始化

### 层次处理阶段
3. `multidriven-guard` - 检查驱动冲突
4. `xmr-resolve` - 解析跨模块引用
5. `strip-debug` - 将 debug 逻辑与主逻辑拆分
6. `repcut` - 对选定模块进行分区
7. `instance-inline` - 将拆分后的 wrapper 回填到父模块
8. `hier-flatten` - 扁平化层次结构

### 优化阶段
9. `latch-transparent-read` - 处理锁存器
10. `slice-index-const` - 优化切片
11. `simplify` - 综合优化（包含常量折叠、冗余消除、死代码消除）

### 调度规划阶段
12. `trigger-key-driven-schedule` - 为目标 graph 构建静态调度分组与拓扑计划

### 验证阶段
13. `comb-loop-elim` - 检查组合循环
14. `stats` - 统计最终规模

## 注意事项

- 某些 passes 会修改 IR，建议在使用前备份设计
- `simplify` pass 已经包含了三个内部 passes，通常不需要单独调用内部 passes
- 部分 passes 有依赖关系，需要按正确顺序执行
- `strip-debug`、`repcut`、`instance-inline` 的 `-path` 都依赖稳定的层次结构；其中 `repcut` / `instance-inline` 的多段路径按实例名解析，不按模块名解析
- `trigger-key-driven-schedule` 要求输入 graph 已完成去层次化和 XMR 规整，并把结果发布到 scratchpad，而不是生成新的 graph
- 使用 `-v` 或 `--verbosity` 选项可以查看详细的 pass 执行信息
