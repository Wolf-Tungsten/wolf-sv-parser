# GRH API 使用指南

GRH（Graph RTL Hierarchy）是基于 SSA 的 RTL 中间表示，本文档介绍其 C++ API 的使用方法。

## 目录

- [1. 概述](#1-概述)
- [2. Netlist 操作](#2-netlist-操作)
- [3. Graph 操作](#3-graph-操作)
- [4. Graph 的双模态](#4-graph-的双模态)

---

## 1. 概述

### 1.1 核心实体

| 实体 | 说明 | IR 层级 |
|------|------|---------|
| **Value** | SSA 数据流边 | 边 |
| **Operation** | 计算节点 | 顶点 |
| **Graph** | 模块容器 | Module |
| **Netlist** | 网表，Graph 的集合 | Design |

```cpp
#include "grh.hpp"
using namespace wolvrix::lib::grh;
```

### 1.2 Value

- **SSA 特性**：每个 Value 只被定义一次，可被多处使用
- **属性**：符号名、位宽、类型（Logic/Real/String）、端口标记
- **关联**：definingOp（定义者）、users（使用者列表）

### 1.3 Operation

- **属性**：操作类型、符号名（可选）、操作数、结果、属性表
- **常用类型**：
  - 常量：`kConstant`
  - 算术：`kAdd`, `kSub`, `kMul`, `kDiv`, `kMod`
  - 位运算：`kAnd`, `kOr`, `kXor`, `kNot`
  - 比较：`kEq`, `kNe`, `kLt`, `kLe`, `kGt`, `kGe`
  - 数据选择：`kMux`, `kConcat`, `kSliceStatic`
  - 存储：`kRegister`, `kMemory`, `kRegisterReadPort`, `kRegisterWritePort`
  - 层次结构：`kInstance`, `kBlackbox`

### 1.4 ID 与命名机制

| ID 类型 | 说明 |
|---------|------|
| `SymbolId` | 符号 ID（仅 Graph 内部使用） |
| `ValueId` | Value 标识 |
| `OperationId` | Operation 标识 |
| `GraphId` | Graph 标识 |

**命名机制**：

| 层级 | 命名方式 |
|------|----------|
| Netlist | Graph 名称直接用字符串 |
| Graph 内部 | Value/Operation/端口使用 Symbol ID |

---

## 2. Netlist 操作

Netlist 管理 Graph 集合及模块层次关系，Graph 名称直接使用字符串。

### 2.1 创建与查询

```cpp
// 创建 Netlist
Netlist netlist;

// 遍历所有 Graph
for (const auto& [name, graphPtr] : netlist.graphs()) {
    Graph& graph = *graphPtr;
}

// 按名称查找
Graph* found = netlist.findGraph("MyModule");
```

### 2.2 创建 Graph

```cpp
// 创建 Graph（同名已存在会抛异常）
Graph& graph = netlist.createGraph("MyModule");

// 安全创建：先查后建
if (!netlist.findGraph("MyModule")) {
    netlist.createGraph("MyModule");
}
```

### 2.3 顶层模块

```cpp
netlist.markAsTop("TopModule");      // 标记顶层
netlist.unmarkAsTop("Testbench");    // 取消标记
bool isTop = netlist.isTopGraph("TopModule");  // 检查

for (const auto& name : netlist.topGraphs()) {  // 遍历顶层
    Graph* top = netlist.findGraph(name);
}
```

### 2.4 Graph 别名

```cpp
// 为参数化模块注册别名
Graph& graph = netlist.createGraph("Module_Width8");
netlist.registerGraphAlias("Module#(8)", graph);

// 查找时别名自动解析
Graph* found = netlist.findGraph("Module#(8)");  // 指向 Module_Width8
```

---

## 3. Graph 操作

Graph 内部使用 Symbol ID 管理命名。创建/绑定操作必须使用 Symbol ID；查询操作支持 Symbol ID 或字符串两种方式。

### 3.1 Symbol ID

Graph 内部使用字符串 interning 机制：每个符号对应唯一的 Symbol ID，避免重复存储字符串。

#### 申请与查找

```cpp
// 申请符号（首次成功，同名返回无效 ID）
SymbolId sym = graph.internSymbol("my_signal");

// 查找已存在符号
SymbolId lookup = graph.lookupSymbol("my_signal");
std::string_view text = graph.symbolText(sym);  // 获取原始字符串
```

#### 处理命名冲突

```cpp
// 方式 1：先查后建
SymbolId sym = graph.lookupSymbol("signal");
if (!sym.valid()) {
    sym = graph.internSymbol("signal");
}

// 方式 2：自动生成内部符号（推荐用于临时变量）
uint32_t counter = 0;
SymbolId sym1 = symbol_utils::makeInternalSymbol(graph, "temp", counter);  // "_temp_0"
SymbolId sym2 = symbol_utils::makeInternalSymbol(graph, "temp", counter);  // "_temp_1"

// 方式 3：仅生成符号文本（不插入符号表）
std::string text = symbol_utils::makeInternalSymbolText("temp", 0);  // "_temp_0"
SymbolId sym = graph.internSymbol(text);  // 需手动插入
```

### 3.2 遍历与查询

```cpp
// 遍历所有 Operation
for (OperationId opId : graph.operations()) {
    Operation op = graph.getOperation(opId);
    if (op.kind() == OperationKind::kAdd) {
        // 处理加法
    }
}

// 遍历所有 Value
for (ValueId valueId : graph.values()) {
    Value value = graph.getValue(valueId);
}

// 遍历端口
for (const auto& port : graph.inputPorts()) {
    std::string_view name = graph.symbolText(port.name);
}

// 按名称查找
ValueId valueId = graph.findValue("signal_name");      // 字符串方式
ValueId valueId2 = graph.findValue(symbolId);          // Symbol ID 方式
```

#### 获取详细信息

```cpp
// Value 信息
Value value = graph.getValue(valueId);
value.symbolText();   // 信号名
value.width();        // 位宽
value.isInput();      // 是否为输入端口
value.definingOp();   // 定义该 Value 的 Operation
value.users();        // 使用者列表

// Operation 信息
Operation op = graph.getOperation(opId);
op.kind();            // 操作类型
op.operands();        // 输入值列表
op.results();         // 输出值列表
op.attr("key");       // 获取属性

// 属性读取
if (auto attr = op.attr("width")) {
    int64_t width = std::get<int64_t>(*attr);
}
```

### 3.3 创建实体

**标准流程：先申请符号，再创建实体。**

```cpp
// 创建 Value
SymbolId sym = graph.internSymbol("data");
ValueId value = graph.createValue(sym, 32, false);  // width=32, unsigned

// 创建 Operation
SymbolId opSym = graph.internSymbol("add");
OperationId op = graph.createOperation(OperationKind::kAdd, opSym);
graph.addOperand(op, lhs);
graph.addOperand(op, rhs);
graph.addResult(op, result);

// 创建常量
SymbolId valSym = graph.internSymbol("const_val");
SymbolId constSym = graph.internSymbol("const_op");
ValueId constVal = graph.createValue(valSym, 8, false);
OperationId constOp = graph.createOperation(OperationKind::kConstant, constSym);
graph.addResult(constOp, constVal);
graph.setAttr(constOp, "constValue", std::string("8'hFF"));

// 绑定端口
SymbolId clkSym = graph.internSymbol("clk");
ValueId clk = graph.createValue(clkSym, 1, false);
graph.bindInputPort(clkSym, clk);
```

### 3.4 修改与删除

```cpp
// 修改 Operation
graph.replaceOperand(op, 0, newValue);              // 替换操作数
graph.setOpKind(op, OperationKind::kMul);           // 修改类型
graph.setAttr(op, "key", value);                    // 设置属性
graph.eraseAttr(op, "key");                         // 删除属性

// 替换 Value 的所有使用
graph.replaceAllUses(oldValue, newValue);

// 删除（安全版会检查是否有使用）
if (graph.eraseOp(op)) { /* 删除成功 */ }
if (graph.eraseValue(value)) { /* 删除成功 */ }

// 删除（强制版，不检查使用，慎用）
graph.eraseOpUnchecked(op);
graph.eraseValueUnchecked(value);
```

---

## 4. Graph 的双模态

Graph 有两种内部状态，影响遍历性能：

| 状态 | 特点 | 触发方式 |
|------|------|----------|
| **可变态** | 支持读写（默认） | 创建后默认 |
| **冻结态** | 只读，遍历更快 | 调用 `freeze()` |

### 冻结与解冻

```cpp
// 冻结以优化遍历性能
graph.freeze();

// 执行写操作时自动解冻
graph.createValue(sym, 32, false);  // 自动回到可变态
```

### 使用建议

```cpp
// 性能敏感场景：多次遍历前先冻结
void analyze(Graph& graph) {
    graph.freeze();
    
    for (auto opId : graph.operations()) { /* ... */ }
    for (auto valueId : graph.values()) { /* ... */ }
    
    // 如需修改，直接调用写 API 会自动解冻
}
```
