# latch-transparent-read

## 功能概述

`latch-transparent-read` pass 通过在读端口插入多路选择器（Mux）来建模锁存器的透明读行为。

## 详细说明

在锁存器（Latch）设计中，当锁存器处于透明模式（update 条件为真）时，读操作应该返回新值而不是存储值。该 pass 自动检测并建模这种行为。

### 透明读行为

对于带有写端口和读端口的锁存器：

- **写端口**：包含 update 条件、新值和可选的掩码
- **读端口**：返回锁存器值

透明读逻辑：
```
result = updateCond ? masked_new_value : latch_value
```

其中 `masked_new_value` 处理部分写入的情况：
```
masked_new_value = (nextValue & mask) | (latch_value & ~mask)
```

## 处理流程

1. **收集锁存器信息**：扫描所有锁存器声明、写端口和读端口
2. **验证约束**：
   - 确保每个锁存器只有一个写端口（避免多驱动冲突）
   - 检查必需的属性（如 `latchSymbol`）
3. **插入 Mux**：为每个读端口创建透明读逻辑
4. **替换使用**：将读端口的原始使用者连接到 Mux 的输出

## 使用示例

```bash
# 基本使用
wolvrix --pass=latch-transparent-read input.sv
```

## 生成操作

对于每个读端口，pass 可能创建以下操作：

| 条件 | 创建的操作 |
|------|-----------|
| 无掩码 | 单个 `kMux` 操作 |
| 有掩码 | `kNot`（掩码取反）、两个 `kAnd`、一个 `kOr`、一个 `kMux` |

## 注意事项

- 该 pass 假设锁存器只有一个写端口，多写端口会导致错误
- 无写端口的锁存器会产生警告
- 生成的 Mux 操作会保留原始位宽和符号信息
- 建议在 `hier-flatten` 之前运行此 pass
