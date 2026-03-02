# hier-flatten

## 功能概述

`hier-flatten` pass 将层次化的模块设计扁平化为单个模块，通过内联所有实例操作来实现。

## 详细说明

该 pass 递归地将子模块的实例内联到父模块中，创建一个扁平化的设计表示。这在综合优化、形式验证和某些仿真流程中非常有用。

### 扁平化过程

1. **实例发现**：识别所有模块实例操作（`kInstance`）
2. **递归内联**：深度优先地内联子模块的运算和值
3. **符号重命名**：使用层级分隔符（`$`）保持名称唯一性
4. **端口连接处理**：将子模块的端口连接转换为内部的运算操作数

### 符号保护模式

| 模式 | 说明 |
|------|------|
| `all` | 保护所有符号（默认） |
| `hierarchy` | 仅保护层级路径符号 |
| `stateful` | 仅保护状态元素（寄存器、锁存器、存储器） |
| `none` | 不保护任何符号 |

## 配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `-preserve-modules` | false | 保留被扁平化的模块定义 |
| `-sym-protect` | `all` | 符号保护模式 |

## 使用示例

```bash
# 基本扁平化
wolvrix --pass=hier-flatten input.sv

# 保留原始模块定义
wolvrix --pass=hier-flatten:-preserve-modules input.sv

# 仅保护状态元素
wolvrix --pass=hier-flatten:-sym-protect=stateful input.sv

# 不保护任何符号
wolvrix --pass=hier-flatten:-sym-protect=none input.sv
```

## 内联命名约定

扁平化后的符号名称遵循以下格式：

```
<parent_prefix>$<instance_name>$_<original_symbol>
```

例如：`top$alu$_result` 表示顶层模块中 `alu` 实例的 `result` 信号。

## 注意事项

- 递归实例（循环层次结构）会被检测并跳过
- 黑盒模块（如果未被转换）不会被内联
- 大量内联可能导致 IR 膨胀，影响内存使用
- 建议在扁平化之前运行 `multidriven-guard` 检测潜在的驱动冲突
