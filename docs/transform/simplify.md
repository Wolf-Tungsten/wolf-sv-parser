# simplify

## 功能概述

`simplify` pass 是一个综合性的优化 pass，迭代地执行常量折叠、冗余消除和死代码消除，以简化设计并提高性能。

## 详细说明

该 pass 通过组合多个子 passes 的迭代执行，实现深度优化。每次迭代都会尝试进一步简化设计，直到没有更多优化可做或达到最大迭代次数。

### 子 Passes

| Pass | 功能 |
|------|------|
| `const-fold` | 常量折叠，计算编译时常量表达式 |
| `redundant-elim` | 冗余消除，内联简单赋值并消除重复运算 |
| `dead-code-elim` | 死代码消除，移除未使用的运算和值 |

### 迭代流程

```
for iter in 0..maxIterations:
    run const-fold
    run redundant-elim
    run dead-code-elim
    if no changes: break
```

## 配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-max-iter` | 8 | 最大迭代次数 |
| `-x-fold` | `known` | X 值处理模式 |
| `-semantics` | `4state` | 语义模式 |

### X 值处理模式

控制常量折叠时如何处理包含 X/Z 值的情况：

| 模式 | 说明 |
|------|------|
| `strict` | 严格模式，操作数包含 X/Z 时不进行折叠 |
| `known` | 已知模式，折叠结果包含 X/Z 时不传播（默认） |
| `propagate` | 传播模式，即使结果包含 X/Z 也传播，仅发出警告 |

### 语义模式

| 模式 | 说明 |
|------|------|
| `2state` | 二值逻辑（0/1） |
| `4state` | 四值逻辑（0/1/X/Z）（默认） |

## 使用示例

```bash
# 基本使用
wolvrix --pass=simplify input.sv

# 增加迭代次数
wolvrix --pass=simplify:-max-iter=16 input.sv

# 使用二值语义
wolvrix --pass=simplify:-semantics=2state input.sv

# 严格 X 处理
wolvrix --pass=simplify:-x-fold=strict input.sv

# 组合配置
wolvrix --pass=simplify:-max-iter=16:-x-fold=strict:-semantics=2state input.sv
```

## 优化效果

- **减少运算数量**：常量折叠消除了运行时计算
- **简化数据流**：冗余消除减少了中间变量
- **降低资源使用**：死代码消除移除了未使用的逻辑

## 注意事项

- 迭代次数过多可能增加编译时间而收益递减
- 二值语义会改变设计的 X/Z 行为
- 建议在综合流程的早期运行此 pass
- 可以通过日志级别查看每次迭代的优化统计
