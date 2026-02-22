# GRH IR 白皮书

GRH（Graph RTL Hierarchy）是基于 SSA 的 RTL 中间表示，用于表示 SystemVerilog 设计的结构和行为。

## 目录

- [1. 核心概念速通](#1-核心概念速通)
- [2. Value 详解](#2-value-详解)
- [3. Operation 详解](#3-operation-详解)
- [4. Graph 详解](#4-graph-详解)
- [5. Netlist 详解](#5-netlist-详解)
- [6. Operation 分类参考](#6-operation-分类参考)

---

## 1. 核心概念速通

| 实体 | 说明 | IR 层级 |
|------|------|---------|
| **Value** | SSA 数据流边 | 边 |
| **Operation** | 计算节点 | 顶点 |
| **Graph** | 模块容器 | Module |
| **Netlist** | 网表，Graph 的集合 | Design |

### 1.1 Value

数据流边，描述 Operation 之间的数据传递关系，满足 SSA 特性（单定义、多使用）。

**字段**：
- `symbol` (`SymbolId`)：信号名（字符串）。Symbol 由 Graph 符号表驻留管理，Value 和 Operation 共享同一符号表，因此信号名不能与 Operation 的符号名冲突
- `width` (`int32_t`)：位宽，仅 Logic 类型有效，必须 > 0
- `type` (`ValueType`)：数据类型
  - `Logic`：四态逻辑（0/1/x/z）
  - `Real`：实数
  - `String`：字符串
- `isSigned` (`bool`)：是否有符号，仅 Logic 类型有效
- `isInput/Output/Inout` (`bool`)：端口标记，通过 `bindXxxPort` 设置
- `definingOp` (`OperationId`)：定义该 Value 的 Operation，端口 Value 为 invalid
- `users` (`ValueUser[]`)：使用该 Value 的 Operation 列表及操作数位置

### 1.2 Operation

操作节点，Graph 的顶点，对输入数据执行特定语义操作并产生输出。涵盖组合计算、数据搬运、存储访问、层次调用等多种行为，通过 `OperationKind` 区分具体语义。

**字段**：
- `kind` (`OperationKind`)：操作类型，决定语义。例如
  - 组合运算：`kAdd`, `kMul`, `kAnd`, `kMux` 等
  - 数据搬运：`kConcat`, `kSliceStatic`, `kAssign` 等
  - 存储访问：`kRegister`, `kMemoryReadPort` 等
  - 层次调用：`kInstance`, `kBlackbox` 等
- `symbol` (`SymbolId`)：符号名（字符串）。Symbol 由 Graph 符号表驻留管理，Value 和 Operation 共享同一符号表，因此符号名不能与 Value 的信号名冲突
- `operands` (`ValueId[]`)：输入值列表，作为操作数的 Value 必须已定义
- `results` (`ValueId[]`)：输出值列表，通常由本 Operation 创建并绑定
- `attrs` (`AttrKV[]`)：属性表，存储额外元数据，如常量值、切片范围等

### 1.3 Graph

Graph 是 Operation 和 Value 的容器，对应一个 SystemVerilog 模块。

**功能**：
- 创建和管理 Operation/Value 及其连接关系
- 声明模块端口（输入/输出/双向）

**示例**：

```sv
module add_sub (
    input  [7:0] a, b,
    input        sel,
    output [7:0] y
);
    assign y = sel ? (a + b) : (a - b);
endmodule
```

对应的 Graph 结构：

```
Graph "add_sub"
├── InputPort "a" -> Value _a (8-bit)
├── InputPort "b" -> Value _b (8-bit)
├── InputPort "sel" -> Value _sel (1-bit)
├── OutputPort "y" -> Value _y (8-bit)
│
├── Operation add (kAdd)
│   ├── operands: [_a, _b]
│   └── result: Value _add_result (8-bit)
│
├── Operation sub (kSub)
│   ├── operands: [_a, _b]
│   └── result: Value _sub_result (8-bit)
│
└── Operation mux (kMux)
    ├── operands: [_sel, _add_result, _sub_result]
    └── result: _y
```

### 1.4 Netlist

Netlist 是 Graph 的集合，代表整个设计（Design）。

**功能**：
- 管理多个 Graph（模块）
- 标记顶层模块（设计入口）
- 支持模块别名（用于参数化模块实例化）

**示例**：

```sv
// 子模块
module child (
    input  [7:0] in,
    output [7:0] out
);
    assign out = in + 1;
endmodule

// 顶层模块
module top;
    wire [7:0] a, b;
    child u_child (.in(a), .out(b));
endmodule
```

对应的 Netlist 结构：

```
Netlist
├── Graph "child"
│   ├── InputPort "in"
│   ├── OutputPort "out"
│   └── Operation add (kConstant 1 + input)
│
├── Graph "top" [顶层]
│   ├── Value a, b (内部信号)
│   └── Operation inst (kInstance of "child")
│       ├── operand: a → 连接到 child.in
│       └── result: b ← 连接到 child.out
│
└── 顶层标记: ["top"]
```

---

## 2. Value 详解

### 2.1 Value 是什么

Value 是 GRH IR 中表示**数据流**的核心抽象，它是连接 Operation 的边，满足 SSA（静态单赋值）特性：

- **单定义**：每个 Value 在 Graph 内只被定义一次（通过某个 Operation 的 result 或作为端口 Value）
- **多使用**：可以被多个 Operation 作为操作数引用

Value 的本质作用：
1. **传递数据**：携带类型、位宽、值等数据信息
2. **建立依赖**：通过 `definingOp` 和 `users` 形成数据流图
3. **表示接口**：端口 Value 作为 Graph 与外部模块交互的边界

### 2.2 字段详解

#### `symbol`

信号名，本质是字符串，由 Graph 的符号表驻留管理。Value 和 Operation 共享同一符号表，因此：
- 同一 Graph 内，Value 的 symbol 与 Operation 的 symbol 不能重复
- 通过 symbol 可以查找对应的 Value（`Graph::findValue`）

#### `type` 与 `width`/`isSigned`

Value 支持三种数据类型：

**Logic**
- 对应 SystemVerilog 的 `logic`/`wire`/`reg`
- 支持四态：0、1、x（未知）、z（高阻）
- 必须指定位宽（`width > 0`）和是否有符号（`isSigned`）

**Real**
- 对应 SystemVerilog 的 `real`
- 用于浮点数运算
- `width` 和 `isSigned` 字段不参与语义

**String**
- 对应 SystemVerilog 的 `string`
- 用于存储文本数据
- `width` 和 `isSigned` 字段不参与语义

#### `definingOp` 与 `users`

这两个字段建立 Value 与 Operation 之间的连接关系：

- `definingOp`：指向**定义**该 Value 的 Operation
  - 端口 Value：invalid（不由 Operation 定义，由外部驱动）
  - 内部 Value：指向产生该值的 Operation

- `users`：记录所有**使用**该 Value 的 Operation
  - 每个条目包含：Operation 引用 + 操作数索引
  - 用于替换、删除时的引用追踪

正向追溯：通过 `definingOp` 可以找到 Value 的**来源**（哪个 Operation 产生了它）
反向追踪：通过 `users` 可以找到 Value 的**去向**（被哪些 Operation 使用）

例如表达式 `c = (a + b) + 1`：
- `add1` Operation 计算 `a + b`，产生中间值 `t`
- `t.definingOp` = `add1`，`t.users` = [`add2`]
- `add2` Operation 计算 `t + 1`，产生结果 `c`
- `c.definingOp` = `add2`，`c.users` = []

#### 端口标记

- `isInput`：是否绑定到输入端口
- `isOutput`：是否绑定到输出端口  
- `isInout`：是否绑定到双向端口

端口 Value 的特殊性：
- `definingOp` 为 invalid（不由 Operation 定义）
- 由外部驱动（input/inout）或驱动外部（output/inout）

### 2.3 数组扁平化

SystemVerilog 的 packed array、struct、union 等复合类型在 GRH 中被扁平化为单个 Logic Value。

**扁平化规则**：
- 多维数组按 packed 布局展开为单个大位宽
- 高位维度在前，低位维度在后
- 结构体按字段声明顺序拼接

**示例**：`input [3:0][7:0] arr`

扁平化为 32 位的 Logic Value：
```
位范围        对应元素
[31:24]  →  arr[3]
[23:16]  →  arr[2]
[15:8]   →  arr[1]
[7:0]    →  arr[0]
```

元素访问通过 `kSliceStatic`（常量索引）或 `kSliceDynamic`（变量索引）Operation 实现。

---

## 3. Operation 详解

### 3.1 Operation 是什么

Operation 是 GRH IR 的**操作节点**，Graph 的顶点，代表对数据或系统执行的各种操作。与 Value 配合形成完整的数据流图。

Operation 的行为由 `kind` 字段决定，不同 kind 对应不同的语义：有的执行算术运算，有的定义存储元件，有的实例化其他模块，有的与仿真环境交互。

Operation 的核心特性：
- **类型驱动**：`kind` 字段决定语义，不同 kind 有不同的操作数要求和结果数量
- **输入输出明确**：通过 `operands` 接收 Value，通过 `results` 产生 Value（部分 Operation 可能没有 results）
- **可携带元数据**：`attrs` 存储额外信息，如常量值、切片范围、实例化参数等

### 3.2 字段详解

#### `kind`

操作类型，由 `OperationKind` 枚举定义，决定 Operation 的语义。主要分类：

- **组合运算**：`kAdd`, `kSub`, `kMul`, `kAnd`, `kOr`, `kMux` 等，输出仅取决于输入
- **数据搬运**：`kConcat`, `kSliceStatic`, `kSliceDynamic`, `kAssign` 等，重组或传递数据
- **时序存储**：`kRegister`, `kMemory`, `kLatch` 及其读写端口，涉及状态保持
- **层次调用**：`kInstance`, `kBlackbox`, `kXMRRead/Write`，与外部模块交互
- **系统调用**：`kSystemFunction`, `kSystemTask`, `kDpicCall`，副作用明确

#### `symbol`

符号名（字符串），由 Graph 符号表驻留管理。与 Value 共享同一符号表，因此：
- Operation 的 symbol 不能与 Value 的信号名重复
- 通过 symbol 可以查找对应的 Operation（`Graph::findOperation`）
- 部分 Operation（如内部临时节点）可能使用工具生成的内部名

#### `operands` 与 `results`

- `operands`：输入 Value 列表，表示操作的数据来源
  - 每个 operand 必须是已定义的 Value
  - operand 的顺序通常有语义（如 kSub 的第0个是被减数，第1个是减数）

- `results`：输出 Value 列表，表示操作产生的数据
  - 通常由本 Operation 创建并绑定
  - 结果数量由 kind 决定：大部分产生1个结果，部分（如 kInstance）可能产生多个

**连接约束**：
- operand 的 Value 必须在同一 Graph 内定义
- result 的 Value 的 definingOp 指向本 Operation
- 删除 Operation 前需处理其 results 的 users

#### `attrs`

属性表，存储 kind 无法表达的额外元数据，键值对形式：`{key: value}`

**key**：字符串（`std::string`），不经符号表驻留，直接存储原始字符串

**value** 支持以下类型（`AttributeValue`）：
- 标量：`bool`、`int64_t`、`double`、`std::string`
- 数组：`std::vector<bool>`、`std::vector<int64_t>`、`std::vector<double>`、`std::vector<std::string>`

**常见属性示例**：
- `kConstant`：`{"constValue": "8'hFF"}`
- `kSliceStatic`：`{"msb": 15, "lsb": 8}`
- `kInstance`：`{"module": "adder", "instanceName": "u_add"}`
- `kRegister`：`{"resetValue": "8'd0", "asyncReset": true}`

### 3.3 Operation 分类

所有 OperationKind 的详细分类介绍请参考 [6. Operation 分类参考](#6-operation-分类参考)。

---

## 4. Graph 详解

（待整理）

---

## 5. Netlist 详解

（待整理）

---

## 6. Operation 分类参考

本章按功能分类介绍所有 Operation，包括操作语义、操作数要求、结果数量及常用属性。

### 6.1 常量

（待整理）

### 6.2 算术运算

（待整理）

### 6.3 位运算

（待整理）

### 6.4 比较运算

（待整理）

### 6.5 逻辑运算

（待整理）

### 6.6 规约运算

（待整理）

### 6.7 移位运算

（待整理）

### 6.8 数据选择

（待整理）

### 6.9 切片

（待整理）

### 6.10 赋值

（待整理）

### 6.11 锁存器

（待整理）

### 6.12 寄存器

（待整理）

### 6.13 存储器

（待整理）

### 6.14 层次结构

（待整理）

### 6.15 XMR

（待整理）

### 6.16 系统调用

（待整理）

### 6.17 DPI

（待整理）
