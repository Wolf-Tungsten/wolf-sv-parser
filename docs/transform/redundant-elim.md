# redundant-elim (内部 Pass)

## 功能概述

`redundant-elim` 是一个内部 pass，由 `simplify` pass 调用，用于内联简单的赋值操作并消除冗余的临时变量。

## 详细说明

该 pass 执行多种局部优化来消除冗余的运算和值，包括赋值内联、公共子表达式消除（CSE）和逻辑优化。

### 优化类型

#### 1. 赋值内联（Assign Inlining）

将简单的 `kAssign` 操作内联到其使用者中：

```
%tmp = kAssign %src
%result = kAdd %tmp, %b
```
优化为：
```
%result = kAdd %src, %b
```

#### 2. 常量传播到输出

将驱动输出端口的常量直接绑定到输出：

```
%const = kConstant { value = 8'hFF }
%out = kAssign %const
```
优化为：
```
%out = kConstant { value = 8'hFF }
```

#### 3. 单操作数连接消除

消除单操作数的 `kConcat`：

```
%result = kConcat %a
```
优化为直接使用 `%a`。

#### 4. 逻辑或优化

检测 `A || !A` 模式并替换为常量 1：

```
%not_a = kLogicNot %a
%result = kLogicOr %a, %not_a
```
优化为：
```
%result = kConstant { value = 1'b1 }
```

#### 5. NOT/XOR 折叠

将 `NOT(XOR(...))` 转换为 `XNOR`：

```
%xor = kXor %a, %b
%result = kNot %xor
```
优化为：
```
%result = kXnor %a, %b
```

#### 6. 公共子表达式消除（CSE）

识别并消除重复的计算：

```
%t1 = kAdd %a, %b
%t2 = kAdd %a, %b  // 重复
```
优化为：
```
%t1 = kAdd %a, %b
// %t2 被替换为 %t1
```

## CSE 候选条件

要成为 CSE 候选，运算必须：

1. 无副作用（不是存储器、实例等）
2. 只有一个结果值
3. 结果不是端口值
4. 结果是临时符号（以 `_val_` 开头）
5. 所有操作数都有效

## 与 Simplify Pass 的关系

该 pass 通常不直接调用，而是通过 `simplify` pass 间接使用：

```bash
# 通过 simplify 间接使用
wolvrix --pass=simplify input.sv
```

## 注意事项

- 这是内部 pass，不对外暴露为独立的 transform 选项
- CSE 使用哈希表进行高效的重复检测
- 属性值在比较时会被排序以确保一致性
- 输出端口的常量驱动会被特殊处理以保留端口名称
