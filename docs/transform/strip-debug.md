# strip-debug

## 功能概述

`strip-debug` pass 按指定路径选中一个 graph，并将其中的 debug 相关逻辑拆分成两个部分：

- `moduleName_logic_part`
- `moduleName_debug_part`

原 graph 名保持不变，但会被重建为一个 wrapper graph，在内部实例化上面两个子图。

## 路径语义

`strip-debug` 使用 `-path` 指定目标，支持两种形式：

- 单段路径：直接按 graph 名查找
- 多段路径：`<root>.<inst>...`

多段路径的解析规则是：

1. 第 1 段必须是 root graph 名
2. 后续每一段都必须匹配当前 graph 中某个 `kInstance` 的 `instanceName`
3. 通过该实例的 `moduleName` 跳到下一级 graph
4. 最终定位到要拆分的 graph

这意味着：

- `top.u_child` 合法，表示“从 `top` 进入实例名为 `u_child` 的子模块”
- `top.child_logic_part` 不一定合法，除非父模块里真的有一个实例名叫 `child_logic_part`

## 会被拆走的内容

`strip-debug` 会把以下操作视为 debug 逻辑并迁移到 `*_debug_part`：

- `kDpicCall`
- `kSystemTask`
- `kInstance`
- `kBlackbox`

这些操作相关的边界输入、边界输出也会在 logic/debug 两个子图之间补桥接端口。

## 重写后的结构

假设原始 graph 名为 `SimTop`，执行：

```bash
wolvrix --pass=strip-debug:-path=SimTop input.json
```

得到的结构是：

- wrapper graph: `SimTop`
- logic graph: `SimTop_logic_part`
- debug graph: `SimTop_debug_part`

其中 wrapper graph 内部会创建两个实例：

- `instanceName = logic_part`, `moduleName = SimTop_logic_part`
- `instanceName = debug_part`, `moduleName = SimTop_debug_part`

注意这里：

- 拆分完成后，若后续还要沿层级路径访问 logic 子图，应该写 `SimTop.logic_part`
- 这里的 `SimTop_logic_part` 是被实例化的 graph / module 名
- 这里的 `logic_part` 是 `SimTop` wrapper 内部的实例名

如果设计里已经存在同名 graph，pass 会为新建的 logic/debug graph 选择一个未占用的名字。例如：

- 期望名：`SimTop_logic_part`
- 若该名字已存在，则可能改成 `SimTop_logic_part_1`

`debug_part` 对应的 graph 也遵循同样的命名规则。

## 示例

### 示例 1：拆分顶层

```bash
wolvrix --pass=strip-debug:-path=SimTop input.json
```

适用于将整个顶层的 debug 逻辑先隔离出去，再对 logic 部分做后续优化。

### 示例 2：拆分层级中的单个子模块

```bash
wolvrix --pass=strip-debug:-path=top.u_core.u_dut input.json
```

这会定位到 `u_dut` 指向的 graph，并只重写这个 graph；父模块结构保持不变。

## 注意事项

- 该 pass 假设目标层次已经稳定；多段路径中的层级段表示实例名
- 该 pass 只拆目标 graph，不会递归拆内部实例指向的子图
- 如果目标 graph 原本是 top graph，重写后会重新标记为 top；否则不会额外提升为 top
- 若设计中仍存在未展开的层级引用，推荐先执行 `xmr-resolve`
