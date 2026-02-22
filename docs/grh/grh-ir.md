# GRH IR 白皮书

GRH（Graph RTL Hierarchy）是基于 SSA 的 RTL 中间表示，用于表示 SystemVerilog 设计的结构和行为。

# 目录

- [1. 核心概念速通](#1-核心概念速通)
- [2. Value 详解](#2-value-详解)
- [3. Operation 详解](#3-operation-详解)
- [4. Graph 详解](#4-graph-详解)
- [5. Netlist 详解](#5-netlist-详解)
- [6. Operation 分类参考](#6-operation-分类参考)
  - [6.1 常量](#61-常量)
  - [6.2 组合运算](#62-组合运算)
  - [6.3 锁存器](#63-锁存器)
  - [6.4 寄存器](#64-寄存器)
  - [6.5 存储器](#65-存储器)
  - [6.6 层次结构](#66-层次结构)
  - [6.7 XMR](#67-xmr)
  - [6.8 系统调用](#68-系统调用)
  - [6.9 DPI](#69-dpi)

---

# 1. 核心概念速通

| 实体 | 说明 | IR 层级 |
|------|------|---------|
| **Value** | SSA 数据流边 | 边 |
| **Operation** | 计算节点 | 顶点 |
| **Graph** | 模块容器 | Module |
| **Netlist** | 网表，Graph 的集合 | Design |

## 1.1 Value

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

## 1.2 Operation

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

## 1.3 Graph

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

## 1.4 Netlist

Netlist 是 Graph 的集合，代表整个设计（Design）。

**功能**：
- 管理多个 Graph（模块）
- 标记顶层模块（设计入口）
- 支持模块别名（为 Graph 提供额外名称）

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

# 2. Value 详解

## 2.1 Value 是什么

Value 是 GRH IR 中表示**数据流**的核心抽象，它是连接 Operation 的边，满足 SSA（静态单赋值）特性：

- **单定义**：每个 Value 在 Graph 内只被定义一次（通过某个 Operation 的 result 或作为端口 Value）
- **多使用**：可以被多个 Operation 作为操作数引用

Value 的本质作用：
1. **传递数据**：携带类型、位宽、值等数据信息
2. **建立依赖**：通过 `definingOp` 和 `users` 形成数据流图
3. **表示接口**：端口 Value 作为 Graph 与外部模块交互的边界

## 2.2 字段详解

### `symbol`

信号名，本质是字符串，由 Graph 的符号表驻留管理。Value 和 Operation 共享同一符号表，因此：
- 同一 Graph 内，Value 的 symbol 与 Operation 的 symbol 不能重复
- 通过 symbol 可以查找对应的 Value（`Graph::findValue`）

### `type` 与 `width`/`isSigned`

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

### `definingOp` 与 `users`

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

### 端口标记

- `isInput`：是否绑定到输入端口
- `isOutput`：是否绑定到输出端口  
- `isInout`：是否绑定到双向端口

端口 Value 的特殊性：
- `definingOp` 为 invalid（不由 Operation 定义）
- 由外部驱动（input/inout）或驱动外部（output/inout）

## 2.3 数组扁平化

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

# 3. Operation 详解

## 3.1 Operation 是什么

Operation 是 GRH IR 的**操作节点**，Graph 的顶点，代表对数据或系统执行的各种操作。与 Value 配合形成完整的数据流图。

Operation 的行为由 `kind` 字段决定，不同 kind 对应不同的语义：有的执行算术运算，有的定义存储元件，有的实例化其他模块，有的与仿真环境交互。

Operation 的核心特性：
- **类型驱动**：`kind` 字段决定语义，不同 kind 有不同的操作数要求和结果数量
- **输入输出明确**：通过 `operands` 接收 Value，通过 `results` 产生 Value（部分 Operation 可能没有 results）
- **可携带元数据**：`attrs` 存储额外信息，如常量值、切片范围、实例化参数等

## 3.2 字段详解

### `kind`

操作类型，由 `OperationKind` 枚举定义，决定 Operation 的语义。主要分类：

- **组合运算**：`kAdd`, `kSub`, `kMul`, `kAnd`, `kOr`, `kMux` 等，输出仅取决于输入
- **数据搬运**：`kConcat`, `kSliceStatic`, `kSliceDynamic`, `kAssign` 等，重组或传递数据
- **时序存储**：`kRegister`, `kMemory`, `kLatch` 及其读写端口，涉及状态保持
- **层次调用**：`kInstance`, `kBlackbox`, `kXMRRead/Write`，与外部模块交互
- **系统调用**：`kSystemFunction`, `kSystemTask`, `kDpicCall`，副作用明确

### `symbol`

符号名（字符串），由 Graph 符号表驻留管理。与 Value 共享同一符号表，因此：
- Operation 的 symbol 不能与 Value 的信号名重复
- 通过 symbol 可以查找对应的 Operation（`Graph::findOperation`）
- 部分 Operation（如内部临时节点）可能使用工具生成的内部名

### `operands` 与 `results`

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

### `attrs`

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

## 3.3 Operation 分类

所有 OperationKind 的详细分类介绍请参考 [6. Operation 分类参考](#6-operation-分类参考)。

---

# 4. Graph 详解

## 4.1 Graph 是什么

Graph 是 GRH IR 中**模块级别**的核心容器，对应一个 SystemVerilog 模块（`module`）。

一个 Graph 包含：
- **Operation 集合**：模块内的所有操作节点
- **Value 集合**：模块内的所有数据流边（SSA 形式）
- **端口声明**：与外部模块交互的接口定义
- **符号表**：管理 Value 和 Operation 的符号名

## 4.2 端口系统

Graph 支持三种端口类型，对应 SystemVerilog 的端口方向：

| 类型 | SystemVerilog | IR 表示 | 说明 |
|------|---------------|---------|------|
| **Input** | `input` | `Port{name, value}` | 外部输入到模块 |
| **Output** | `output` | `Port{name, value}` | 模块输出到外部 |
| **Inout** | `inout` | `InoutPort{name, in, out, oe}` | 双向端口，三态分解 |

### 4.2.1 Input / Output 端口

Input 和 Output 端口通过 **Value** 与 Graph 内部连接：

- **Input**：外部驱动 Value，内部可以读取
  - Value 的 `definingOp` 为 invalid（不由内部 Operation 定义）
  - Value 标记为 `isInput = true`

- **Output**：内部驱动 Value，外部可以读取  
  - Value 由某个 Operation 的结果产生
  - Value 标记为 `isOutput = true`

### 4.2.2 Inout 端口的三态分解

SystemVerilog 的 `inout` 在 GRH 中被分解为**三个信号**：

| 信号 | 方向 | 说明 |
|------|------|------|
| `in` | Input | 外部输入到模块的数据 |
| `out` | Output | 模块输出到外部的数据 |
| `oe` | Input | 输出使能（Output Enable），控制三态门 |

**示例**：GPIO Pin 控制器

SystemVerilog 实现：
```sv
module gpio_pin (
    inout  wire       pad,      // 物理引脚，双向
    input  wire       dir,      // 方向控制：1=输出，0=输入
    input  wire       drv_val,  // 输出驱动值
    output wire       rd_val    // 读取到的输入值
);
    // 三态门控制
    assign pad = dir ? drv_val : 1'bz;
    assign rd_val = pad;
endmodule
```

对应的 GRH 结构：

```
Graph "gpio_pin"
│
├── InoutPort "pad"
│   ├── in:  Value pad_in      (1-bit)  ← 外部输入到模块
│   ├── out: Value pad_out     (1-bit)  → 模块输出到外部
│   └── oe:  Value pad_oe      (1-bit)  ← 输出使能
│
├── InputPort "dir" → Value dir         (1-bit)
├── InputPort "drv_val" → Value drv_val (1-bit)
├── OutputPort "rd_val" → Value rd_val  (1-bit)
│
├── Operation pad_out_assign (kAssign)
│   ├── operands: [drv_val]
│   └── result: pad_out
│
├── Operation pad_oe_assign (kAssign)
│   ├── operands: [dir]
│   └── result: pad_oe
│
└── Operation rd_val_assign (kAssign)
    ├── operands: [pad_in]
    └── result: rd_val
```

**信号流向说明**：
- 当 `dir = 1`（输出模式）：`pad_oe = 1`，`pad_out = drv_val`，外部读取 `drv_val`
- 当 `dir = 0`（输入模式）：`pad_oe = 0`，`pad_out` 无效，模块通过 `pad_in` 读取外部信号
- `rd_val` 始终反映引脚当前状态（无论输入还是输出模式）

这种分解使得三态逻辑的语义更加明确，便于后续的分析和综合。

## 4.3 符号系统（Symbol System）

**符号（Symbol）**是 Graph 内用于**标识** Value 和 Operation 的字符串名称，可以理解为标识符（Identifier）。每个 Symbol 在 Graph 内通过 **Symbol Table**（符号表）进行管理。

在 GRH IR 中：
- 每个 **Value** 必须关联一个 Symbol（如信号名 `data_reg`）
- 每个 **Operation** 必须关联一个 Symbol（如操作名 `add_op`）
- **Symbol Table** 负责 Symbol 的分配、去重和查找

Symbol 的作用：
1. **可读性**：在调试和 emit 输出时显示有意义的名称
2. **可查找性**：通过 Symbol 定位特定的 Value 或 Operation
3. **调试映射**：关联回原始 SystemVerilog 源码中的标识符

### 4.3.1 符号唯一性约束

**关键约束**：同一 Graph 的 Symbol Table 内，所有 Symbol **必须唯一**，任意两个 Value、或任意两个 Operation、或 Value 与 Operation 之间都不能共享同一个 Symbol。

示例（非法）：
```
Graph  // 错误！Symbol 冲突
├── Value with Symbol "data"       // 第一个 "data"
├── Value with Symbol "data"       // 错误：Value 之间 Symbol 重复
├── Operation with Symbol "add"
└── Operation with Symbol "add"    // 错误：Operation 之间 Symbol 重复
```

此外，Value 与 Operation 的 Symbol 也不能重复：
```
Graph  // 错误！Symbol 冲突
├── Value with Symbol "foo"
└── Operation with Symbol "foo"    // 错误：不能与 Value 的 Symbol 重复
```

### 4.3.2 声明符号（Declared Symbol）

Graph 维护一个 **Declared Symbol** 列表，记录来自用户源码的显式声明标识符。

**用途**：
- 区分**用户声明的信号**（Declared Symbol）与工具生成的内部 Symbol
- 在代码生成（emit）阶段优先保留用户命名的 Symbol
- 死代码消除时保护声明但未使用的 Symbol（用于调试或保留接口）

**示例**：
```sv
module example (
    input  wire a,      // "a" 是 Declared Symbol
    output wire b       // "b" 是 Declared Symbol
);
    wire temp;          // "temp" 是 Declared Symbol
    assign b = a;
endmodule
```

上述模块的 Declared Symbol 列表包含：`a`、`b`、`temp`

即使 `temp` 未被使用，由于它是 Declared Symbol，工具可能会保留它用于调试或报告。

---

# 5. Netlist 详解

## 5.1 Netlist 是什么

Netlist 是 GRH IR 中**设计级别**的顶层容器，代表整个 SystemVerilog 设计（Design）。它是 Graph 的集合，容纳设计中的全部模块及其层次关系。

一个 Netlist 包含：
- **Graph 集合**：设计中的所有模块
- **顶层模块标记**：指定设计的入口点
- **模块别名映射**：为 Graph 提供额外名称，便于查找/输出
- **Netlist Symbol Table**：管理 Netlist 级符号（Graph 名称、Declared Symbol 等）

## 5.2 顶层模块

Netlist 支持标记**一个或多个**顶层模块（Top-Level Module）。

## 5.3 参数化模块处理

SystemVerilog 支持参数化模块（Parameterized Module）：
```sv
module adder #(parameter WIDTH = 8) (input [WIDTH-1:0] a, b, output [WIDTH-1:0] y);
    assign y = a + b;
endmodule

adder #(8)  u1 (...);  // 8位加法器
adder #(16) u2 (...);  // 16位加法器
```

**GRH IR 处理方式**：
- 不同参数值的实例产生**不同的 Graph**
- 因为内部 Value 的位宽不同，Operation 的结构也不同

上例在 GRH 中表示为：
```
Netlist
├── Graph "adder__8"   // WIDTH=8 的实例
│   └── Value 位宽为 8
├── Graph "adder__16"  // WIDTH=16 的实例
│   └── Value 位宽为 16
└── Graph "top"
    ├── Operation kInstance of "adder__8"
    └── Operation kInstance of "adder__16"
```

## 5.4 Netlist 符号系统

Netlist 维护 **Netlist Symbol Table** 管理 Graph 名称，与 Graph 内部的 Symbol Table 形成两级架构。

### 5.4.1 两级架构

| 符号表 | 作用域 | 管理对象 |
|--------|--------|----------|
| **Netlist Symbol Table** | Netlist 级别 | Graph 名称 |
| **Graph Symbol Table** | Graph 级别 | Value 和 Operation 的 Symbol |

Netlist Symbol Table 中每个 Graph 名称必须唯一。

### 5.4.2 模块别名

**模块别名（Graph Alias）**允许一个 Graph 拥有多个名称，同样由 Netlist Symbol Table 管理。

**应用场景**：
- **提高可读性**：为自动生成的唯一 Graph 名称（如 `adder__p_WIDTH_8`）提供简洁别名（如 `adder_8bit`）
- **保留原始名称**：防止参数化的顶层模块在 emit 时被强制改名，通过别名保留用户声明的模块名

---

# 6. Operation 分类参考

本章按功能分类介绍所有 Operation，包括操作语义、操作数要求、结果数量及常用属性。

## 6.1 常量

### kConstant

**operands**: 无

**results**:
- `res[0]`: 产生的常量值

**attrs**:
- `constValue` (string): Verilog 常量语法，如 `"8'hEF"`、`"16'sd-5"`，支持含 `x`/`z`

**语义**:
```
res[0] = constValue
```

---

## 6.2 组合运算

输出仅取决于当前输入的组合逻辑运算。

### 6.2.1 算术运算

四态语义：任一操作数位含 `X`/`Z` 时，结果为全 `X`。

**operands**:
- `oper[0]` (`L`): 左操作数
- `oper[1]` (`R`): 右操作数

**results**:
- `res[0]`: 运算结果

**attrs**: 无

| 操作符 | 语义 | `res[0]` 位宽 |
|--------|------|---------------|
| `kAdd` | `oper[0] + oper[1]` | `max(L, R)` |
| `kSub` | `oper[0] - oper[1]` | `max(L, R)` |
| `kMul` | `oper[0] * oper[1]` | `L + R` |
| `kDiv` | `oper[0] / oper[1]` | `L`（除数 0 行为由 SV 标准定义）|
| `kMod` | `oper[0] % oper[1]` | `R`（除数 0 行为由 SV 标准定义）|

---

### 6.2.2 位运算

按位逻辑运算。四态语义：遵循 SV 四态真值表；`0`/`1` 优先确定，无法确定时返回 `X`。

**二元位运算（kAnd / kOr / kXor / kXnor）**

- **operands**: `oper[0]` (`L`), `oper[1]` (`R`)
- **results**: `res[0]`，位宽 `max(L, R)`
- **attrs**: 无

| 操作符 | 语义 |
|--------|------|
| `kAnd` | `oper[0] & oper[1]` |
| `kOr` | `oper[0] \| oper[1]` |
| `kXor` | `oper[0] ^ oper[1]` |
| `kXnor` | `oper[0] ~^ oper[1]` |

**一元位运算（kNot）**

- **operands**: `oper[0]`
- **results**: `res[0]`，位宽 `width(oper[0])`
- **attrs**: 无
- **语义**: `res[0] = ~oper[0]`

---

### 6.2.3 比较运算

关系比较运算，结果均为 1-bit Logic。

**operands**:
- `oper[0]` (`L`): 左操作数
- `oper[1]` (`R`): 右操作数

**results**:
- `res[0]`: 比较结果，1-bit

**attrs**: 无

| 操作符 | 语义 | 四态处理 |
|--------|------|----------|
| `kLt` | `oper[0] < oper[1]` | 任一位含 `X`/`Z` 时结果为 `X` |
| `kLe` | `oper[0] <= oper[1]` | 任一位含 `X`/`Z` 时结果为 `X` |
| `kGt` | `oper[0] > oper[1]` | 任一位含 `X`/`Z` 时结果为 `X` |
| `kGe` | `oper[0] >= oper[1]` | 任一位含 `X`/`Z` 时结果为 `X` |
| `kEq` | `oper[0] == oper[1]` | 任一位含 `X`/`Z` 且不形成确定不等时结果为 `X` |
| `kNe` | `oper[0] != oper[1]` | 任一位含 `X`/`Z` 且不形成确定不等时结果为 `X` |
| `kCaseEq` | `oper[0] === oper[1]` | 按位精确比较（含 `X`/`Z`），结果恒为 `0`/`1` |
| `kCaseNe` | `oper[0] !== oper[1]` | 按位精确比较（含 `X`/`Z`），结果恒为 `0`/`1` |
| `kWildcardEq` | `oper[0] ==? oper[1]` | 任一操作数的 `X`/`Z` 视为通配符，结果恒为 `0`/`1` |
| `kWildcardNe` | `oper[0] !=? oper[1]` | 任一操作数的 `X`/`Z` 视为通配符，结果恒为 `0`/`1` |

---

### 6.2.4 逻辑运算

先将操作数规约为 1-bit 逻辑值 `{0,1,X}` 再计算，`X` 保持传播。

**二元逻辑运算（kLogicAnd / kLogicOr）**

- **operands**: `oper[0]`, `oper[1]`
- **results**: `res[0]`，1-bit
- **attrs**: 无

| 操作符 | 语义 |
|--------|------|
| `kLogicAnd` | `oper[0] && oper[1]` |
| `kLogicOr` | `oper[0] \|\| oper[1]` |

**一元逻辑运算（kLogicNot）**

- **operands**: `oper[0]`
- **results**: `res[0]`，1-bit
- **attrs**: 无
- **语义**: `res[0] = !oper[0]`

---

### 6.2.5 规约运算

对操作数所有位进行归约运算，结果为 1-bit。任一位含 `X`/`Z` 且无法确定结果时返回 `X`。

**operands**:
- `oper[0]`: 操作数

**results**:
- `res[0]`: 运算结果，1-bit

**attrs**: 无

| 操作符 | 语义 |
|--------|------|
| `kReduceAnd` | `&oper[0]` |
| `kReduceNand` | `~&oper[0]` |
| `kReduceOr` | `\|oper[0]` |
| `kReduceNor` | `~\|oper[0]` |
| `kReduceXor` | `^oper[0]` |
| `kReduceXnor` | `~^oper[0]` |

---

### 6.2.6 移位运算

四态语义：任一操作数位含 `X`/`Z` 时，结果为全 `X`。

**operands**:
- `oper[0]` (`L`): 被移位操作数
- `oper[1]`: 移位位数（无符号解释）

**results**:
- `res[0]`: 移位结果，位宽 `L`

**attrs**: 无

| 操作符 | 语义 |
|--------|------|
| `kShl` | `oper[0] << oper[1]`，逻辑左移 |
| `kLShr` | `oper[0] >> oper[1]`，逻辑右移 |
| `kAShr` | `oper[0] >>> oper[1]`，算术右移 |

---

### 6.2.7 数据选择（kMux）

**operands**:
- `oper[0]`: 选择条件（1-bit）
- `oper[1]` (`W`): 真分支值
- `oper[2]` (`W`): 假分支值

**results**:
- `res[0]`: 选择结果，位宽 `W`

**attrs**: 无

**语义**:
```
res[0] = oper[0] ? oper[1] : oper[2]
```

四态语义：
- `oper[0] = 1` 时，`res[0] = oper[1]`
- `oper[0] = 0` 时，`res[0] = oper[2]`
- `oper[0] = X/Z` 时，逐位融合：`oper[1][i] == oper[2][i]` 则取该值，否则为 `X`

---

### 6.2.8 切片

位/数组切片操作，用于从信号中提取部分位或数组元素。

**kSliceStatic**（静态常量切片）

**operands**:
- `oper[0]`: 被截取信号（位宽 `W`）

**results**:
- `res[0]`: 截取结果，位宽 `sliceEnd - sliceStart + 1`

**attrs**:
- `sliceStart` (int64_t): 起始位（含），LSB=0
- `sliceEnd` (int64_t): 结束位（含），要求 `sliceEnd >= sliceStart`

**语义**:
- 当 `sliceStart == sliceEnd` 时：`res[0] = oper[0][sliceStart]`（1-bit 位选择）
- 当 `sliceStart < sliceEnd` 时：`res[0] = oper[0][sliceEnd : sliceStart]`（范围选择）

---

**kSliceDynamic**（动态偏移切片）

**operands**:
- `oper[0]`: 被截取信号（位宽 `W`）
- `oper[1]`: 起始偏移（无符号解释）

**results**:
- `res[0]`: 截取结果，位宽 `sliceWidth`

**attrs**:
- `sliceWidth` (int64_t): 截取位宽，必须大于 0

**语义**:
- 当 `sliceWidth == 1` 时：`res[0] = oper[0][oper[1]]`（1-bit 位选择）
- 当 `sliceWidth > 1` 时：`res[0] = oper[0][oper[1] +: sliceWidth]`（索引部分选择）

---

**kSliceArray**（数组元素访问）

**operands**:
- `oper[0]`: 扁平化数组信号（位宽 `W`）
- `oper[1]`: 数组下标（无符号解释）

**results**:
- `res[0]`: 数组元素，位宽 `sliceWidth`

**attrs**:
- `sliceWidth` (int64_t): 单个元素位宽，必须整除 `W`

**语义**:
```
res[0] = oper[0][oper[1] * sliceWidth +: sliceWidth]
```

**说明**: 多维数组访问通过 `kSliceArray` 级联实现

---

### 6.2.9 赋值与数据重组

**kAssign**（连续赋值）

- **operands**: `oper[0]`（输入信号，位宽 `W`）
- **results**: `res[0]`，位宽 `W`
- **attrs**: 无
- **语义**: `res[0] = oper[0]`

**kConcat**（位拼接）

- **operands**: `oper[0]`, `oper[1]`, ..., `oper[N-1]`（待拼接信号）
- **results**: `res[0]`，位宽 `sum(width(oper[i]))`
- **attrs**: 无
- **语义**: `res[0] = {oper[0], oper[1], ..., oper[N-1]}`
- **说明**: `oper[0]` 在高位，`oper[N-1]` 在低位

**kReplicate**（位复制）

- **operands**: `oper[0]`（被复制信号，位宽 `W`）
- **results**: `res[0]`，位宽 `W * rep`
- **attrs**:
  - `rep` (int64_t): 复制次数，必须大于 0
- **语义**: `res[0] = {rep{oper[0]}}`

## 6.3 锁存器

（待整理）

## 6.4 寄存器

（待整理）

## 6.5 存储器

（待整理）

## 6.6 层次结构

（待整理）

## 6.7 XMR

（待整理）

## 6.8 系统调用

（待整理）

## 6.9 DPI

（待整理）
