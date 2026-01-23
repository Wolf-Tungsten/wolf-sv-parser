# GRH JSON 规范（EmitJSON）

EmitJSON 输出 GRH 网表的紧凑 JSON 表示，统一 CLI 与测试的入口。默认写入 `grh.json`（位于 `EmitOptions::outputDir`，缺省为当前工作目录），并通过 `EmitOptions::jsonMode` 控制排版：
- `Compact`: 单行最紧凑格式。
- `PrettyCompact`（默认）：保持顶层/Graph 缩进，但 `vals`/`ports`/`ops` 元素各占一行。
- `Pretty`: 全量缩进换行（每个字段独立一行）。

## 顶层结构
- `graphs`: 按图名升序排列的数组，每个元素为单个 Graph 描述。
- `tops`: 顶层图名数组，顺序遵循 `Emit` 解析后的顶层列表（包括 `topOverrides`）。

## Graph 结构（按字段顺序输出）
- `symbol`: 图名称。
- `vals`: 值列表，保持创建顺序。每个值包含：
  - `sym`: 符号名。
  - `w`: 位宽。
  - `sgn`: 是否有符号。
  - `in` / `out` / `inout`: 输入/输出/inout 标记（不可同时为真）。
  - `def`: 可选，定义该值的 Operation 符号。
  - `users`: 使用者数组 `{ op, idx }`，描述操作与操作数索引。
  - `loc`: 可选，源码位置 `{ file, line, col, endLine, endCol }`。
- `ports`: 端口对象。
  - `in`: 输入端口数组，每项 `{ name, val }`。
  - `out`: 输出端口数组，每项 `{ name, val }`。
  - `inout`: inout 端口数组，每项 `{ name, in, out, oe }`。
- `ops`: 操作列表，保持创建顺序。每个操作包含：
  - `sym`: 符号名。
  - `kind`: 操作类型字符串（来自 `grh::toString(OperationKind)`）。
  - `in` / `out`: 操作数与结果符号数组，保持插入顺序。
  - `attrs`: 可选属性对象，键为属性名，值为属性负载。
  - `loc`: 可选，源码位置 `{ file, line, col, endLine, endCol }`。

## 属性编码
- 属性负载统一为对象 `{ t: <type>, v?: <scalar>, vs?: <array> }`。
- 支持的 `t`：`bool`、`int`、`double`、`string`、`bool[]`、`int[]`、`double[]`、`string[]`。
- 标量使用 `v`，数组使用 `vs`，数组条目保持给定顺序；非有限浮点将被拒绝。

## 排序与布局约定
- Graph 列表按名称排序；`vals`、`ops` 保留创建顺序；端口数组顺序来自 Graph 端口列表（GraphView 冻结路径会按端口名排序）；`attrs` 保持插入顺序。
- `Compact` 模式无多余空格与换行；`PrettyCompact` 模式下 `vals`、`ports.in/out`、`ops` 数组的元素均单行展示；`Pretty` 模式每个字段独立换行。

## 兼容性
- Netlist 解析要求使用 `graphs/vals/ports/ops` 等新键名；属性对象支持 `t/k/kind` 与 `v/value`、`vs/values` 的兼容别名。

## 示例
- `tests/data/emit/demo_expected.json` 提供了压缩键风格的示例输出片段，可用于格式对齐或黄金回归。
