# instance-inline

## 功能概述

`instance-inline` pass 按路径选择一个具体实例，并仅将这一层实例展开到父 graph 中。

该 pass 用于做“单点、一层”的局部 inline。

## 路径语义

`instance-inline` 使用 `-path` 指定目标实例，格式必须是：

```text
<root>.<inst>...
```

`instance-inline` 不接受单段路径；因为它操作的是“某个实例”，而不是“某个 graph”。

解析规则：

1. 第 1 段必须是 root graph 名
2. 后续每一段都按当前 graph 中某个实例的 `instanceName` 匹配
3. 找到实例后，读取其 `moduleName` 进入下一级 graph
4. 最后一段对应的实例就是要 inline 的目标

例如：

- `top.u_mid.u_child` 表示“在 `top` 中找到 `u_mid`，再在 `mid` graph 中找到 `u_child`，然后只 inline `u_child` 这一层”

## 行为特征

### 只展开一层

若目标实例内部还有实例，这些内部实例会保留，不会继续递归 inline。

例如：

- `top.u_mid.u_child` 被 inline 后
- `u_child` 内部若还有 `u_leaf`
- 那么 `u_leaf` 会原样留在父 graph 中

### 目标实例会被删除

inline 成功后，父 graph 中原来的目标 `kInstance` 会被移除，子 graph 的值和操作会克隆到父 graph 中。

### 名字会带层级前缀

为避免符号冲突，克隆出来的 stateful / 内部对象会按路径段生成层级前缀，使用 `$` 连接。

例如路径：

```text
top.u_mid.u_child
```

会生成前缀：

```text
u_mid$u_child
```

因此被 inline 子图中的 `state` 可能变成：

```text
u_mid$u_child$state
```

## 端口连接处理

`instance-inline` 会根据实例上的：

- `inputPortName`
- `outputPortName`
- `inoutPortName`

与子 graph 的端口逐一匹配，建立 value 映射：

- 子 graph 输入端口 -> 父 graph 中实例的 operands
- 子 graph 输出端口 -> 父 graph 中实例的 results
- 输出端口若需要 alias，会补 `kAssign`

如果端口名表与子 graph 端口不一致，pass 会报错失败。

## 示例

### 示例 1：内联一个普通子实例

```bash
wolvrix --pass=instance-inline:-path=top.u_core input.json
```

这会把 `u_core` 对应的子 graph 展开到 `top` 中。

### 示例 2：内联 wrapper 中的子实例

```bash
wolvrix --pass=instance-inline:-path=SimTop.logic_part input.json
```

这里会展开 `SimTop` 中实例名为 `logic_part` 的那一层。

注意：

- 路径中的最后一段写的是实例名 `logic_part`
- `SimTop_logic_part` 若存在，它表示目标实例的 `moduleName`

## 前置条件

- 当前实现要求设计里相关 XMR 已经解析完成；若存在 `kXMRRead` / `kXMRWrite`，pass 会拒绝执行
- 推荐在 `xmr-resolve` 之后运行

## 注意事项

- 该 pass 只 inline 目标实例一层，不会递归处理其内部实例
- 多段路径按实例名解析，不按模块名解析
- 若某层存在重复 `instanceName`，pass 会报错，因为路径无法唯一解析
- 该 pass 适合做定点结构改写与局部展开
