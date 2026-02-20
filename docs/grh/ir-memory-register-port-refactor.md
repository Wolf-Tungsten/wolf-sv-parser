# GRH IR 存储单元 Port 模型重构规划

## 背景与目标

当前 GRH IR 中 `kMemory`、`kRegister`、`kLatch` 的设计不一致，且 `kRegister` 和 `kLatch` 将存储声明与读写逻辑耦合在同一个 Operation 中。为贴合 SystemVerilog 语义、提升 IR 的正交性和优化友好性，本规划将三者统一为**声明 + Port 读写**的模型。

## 核心设计原则

1. **统一模型**：`kMemory`、`kRegister`、`kLatch` 均采用 **声明操作 + ReadPort + WritePort** 的三元结构
2. **SSA 友好**：读写操作作为独立节点，便于数据流分析和优化
3. **贴合 SV 语义**：区分存储声明（reg/array）和过程块中的读写逻辑
4. **支持多写**：`kRegister` 允许存在多个 `kRegisterWritePort`（多 always 块写入场景）

---

## 新 IR 定义

### 1. kMemory / kMemoryReadPort / kMemoryWritePort（保持现状）

`kMemory` 系列已符合 Port 模型，作为参考标准：

```
kMemory
- operands: 无
- results: 无
- attrs: symbol, width, row, isSigned

kMemoryReadPort
- operands: addr
- results: data
- attrs: memSymbol

kMemoryWritePort
- operands: updateCond, addr, data, mask, event0, event1...
- results: 无
- attrs: memSymbol, eventEdge[]
```

---

### 2. kRegister / kRegisterReadPort / kRegisterWritePort（新设计）

#### kRegister（纯声明）

```
kRegister
- operands: 无
- results: 无
- attrs:
    - symbol (string, required): 寄存器名，符合 Verilog 标识符规范
    - width (int64_t): 位宽，必须大于 0
    - isSigned (bool): 是否有符号
```

**生成语义**：
```systemverilog
reg ${isSigned ? "signed " : ""}[${width}-1:0] ${symbol};
```

#### kRegisterReadPort（读端口）

```
kRegisterReadPort
- operands: 无  // 寄存器是标量，无需地址
- results:
    - data: 读出的数据值
- attrs:
    - regSymbol (string, required): 指向目标 kRegister 的 symbol
```

**生成语义**：
```systemverilog
assign ${data.symbol} = ${regSymbol};
```

#### kRegisterWritePort（写端口，允许多个）

```
kRegisterWritePort
- operands:
    - updateCond: 更新条件，1-bit，无条件时绑定 `1'b1`
    - nextValue: 写入值表达式（reset/enable 优先级已在此编码）
    - mask: 写掩码，位宽与目标 kRegister 一致，1 表示该位会被写入；全写时绑定常量全 1
    - event0, event1, ...: 触发事件信号（可变数量）
- results: 无
- attrs:
    - regSymbol (string, required): 指向目标 kRegister 的 symbol
    - eventEdge (string[]): 触发边沿列表，"posedge"/"negedge"，长度与事件信号数量一致
```

**重要约束**：一个 SV `always` 块中对同一寄存器的所有写入逻辑必须**合成为一个** `kRegisterWritePort`。

例如，以下 SV 代码：
```systemverilog
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) reg_a <= 0;
    else if (en) reg_a <= data;
end
```

应转换为**一个** `kRegisterWritePort`，其中：
- `updateCond` = `!rst_n || en`
- `nextValue` = `!rst_n ? 0 : data`（通过 kMux 编码优先级）
- `mask` = 全 1（完整写入）

**禁止**拆分为两个 `kRegisterWritePort`。允许多个 `kRegisterWritePort` 的场景是**多个独立的 always 块**（如多时钟域、测试模式等），而非单个 always 块内的多个条件分支。

**生成语义**：
```systemverilog
always @(${eventEdge[0]} ${event0.symbol}, ${eventEdge[1]} ${event1.symbol}, ...) begin
    if (${updateCond.symbol}) begin
        // mask 控制部分写入
        for (int i = 0; i < ${regSymbol.width}; i = i + 1) begin
            if (${mask.symbol}[i]) begin
                ${regSymbol}[i] <= ${nextValue.symbol}[i];
            end
        end
    end
end
```

当 `mask` 为全 1 时，可简化为：
```systemverilog
always @(${eventEdge[0]} ${event0.symbol}, ...) begin
    if (${updateCond.symbol}) begin
        ${regSymbol} <= ${nextValue.symbol};
    end
end
```

**多写端口示例（完整写入）**：
```systemverilog
// 功能场景：多时钟域或测试模式切换
reg [31:0] reg_a;

// WritePort #1: 正常工作模式
always @(posedge clk) begin
    if (en) reg_a <= data_normal;
end

// WritePort #2: 测试模式
always @(posedge test_clk) begin
    if (test_en) reg_a <= data_test;
end
```

**多写端口示例（部分写入）**：
```systemverilog
// 功能场景：不同时钟域写入不同位段
reg [7:0] reg_a;

// WritePort #1: 写低4位
always @(posedge clk1) begin
    reg_a[3:0] <= data1;
end

// WritePort #2: 写高4位
always @(posedge clk2) begin
    reg_a[7:4] <= data2;
end
```

对应的 GRH IR：
```
kRegister { symbol: "reg_a", width: 8, isSigned: false }

kRegisterWritePort {
    operands: [1'b1, nextValue1, 8'h0F, clk1],  // mask = 00001111
    attrs: { regSymbol: "reg_a", eventEdge: ["posedge"] }
}

kRegisterWritePort {
    operands: [1'b1, nextValue2, 8'hF0, clk2],  // mask = 11110000
    attrs: { regSymbol: "reg_a", eventEdge: ["posedge"] }
}
```

---

### 3. kLatch / kLatchReadPort / kLatchWritePort（新设计）

#### kLatch（纯声明）

```
kLatch
- operands: 无
- results: 无
- attrs:
    - symbol (string, required): 锁存器名
    - width (int64_t): 位宽
    - isSigned (bool): 是否有符号
```

**生成语义**：
```systemverilog
reg ${isSigned ? "signed " : ""}[${width}-1:0] ${symbol};
```

#### kLatchReadPort（读端口）

```
kLatchReadPort
- operands: 无
- results:
    - data: 读出的数据值
- attrs:
    - latchSymbol (string, required): 指向目标 kLatch 的 symbol
```

**生成语义**：
```systemverilog
assign ${data.symbol} = ${latchSymbol};
```

#### kLatchWritePort（写端口，单一）

> **注意**：Latch 为电平敏感，多写语义模糊且易产生综合警告，**限制为单一写端口**。

```
kLatchWritePort
- operands:
    - updateCond: 更新条件，1-bit
    - nextValue: 写入值
    - mask: 写掩码，位宽与目标 kLatch 一致，1 表示该位会被写入；全写时绑定常量全 1
- results: 无
- attrs:
    - latchSymbol (string, required): 指向目标 kLatch 的 symbol
```

**重要约束**：与 `kRegisterWritePort` 相同，一个 SV `always_latch` 块中对同一锁存器的所有写入逻辑必须**合成为一个** `kLatchWritePort`。

例如：
```systemverilog
always_latch begin
    if (!rst_n) q = 0;
    else if (en) q = d;
end
```

应转换为**一个** `kLatchWritePort`，其中：
- `updateCond` 和 `nextValue` 按优先级编码
- `mask` = 全 1（完整写入）

**生成语义**（带 mask）：
```systemverilog
always_latch begin
    if (${updateCond.symbol}) begin
        for (int i = 0; i < ${latchSymbol.width}; i = i + 1) begin
            if (${mask.symbol}[i]) begin
                ${latchSymbol}[i] = ${nextValue.symbol}[i];
            end
        end
    end
end
```

当 `mask` 为全 1 时，可简化为：
```systemverilog
always_latch begin
    if (${updateCond.symbol}) begin
        ${latchSymbol} = ${nextValue.symbol};
    end
end
```

---

## 访问存储单元的 Port 原则

### 核心原则：**所有对存储单元的访问必须通过 Port**

无论是内部逻辑还是 XMR（跨模块引用），对 `kRegister`/`kLatch`/`kMemory` 的读写都必须通过对应的 ReadPort/WritePort。

| 操作类型 | 内部访问 | XMR 跨模块访问 | 生成的 Operation |
|---------|---------|---------------|-----------------|
| **Read** | `kRegisterReadPort` | `kRegisterReadPort` | 统一通过 ReadPort |
| **Write** | `kRegisterWritePort` | `kRegisterWritePort` | 统一通过 WritePort |

### 设计理由

1. **SSA 数据流完整性**：所有数据流都通过显式的 Port 节点，便于追踪 use-def 链
2. **优化友好**：DCE、常量传播等 Pass 可以统一处理所有读取点
3. **扇出分析**：通过统计 ReadPort 数量，可以准确计算寄存器的 fanout
4. **Emit 简化**：无需区分内部读取和 XMR 读取，统一生成 `assign` 或 wire 引用

### 禁止直接 Symbol 引用

~~`assign out = reg_a;`~~ （通过 symbol 直接读取）❌

**正确**：必须通过 `kRegisterReadPort` → `assign out = read_port_result;` ✅

```systemverilog
// 模块内部使用 reg_a
kRegisterReadPort { regSymbol: "reg_a", results: ["reg_a_val"] }
// 生成：assign out = reg_a_val;  // reg_a_val 是 read port 的 result

// XMR 跨模块读取 reg_a
kRegisterReadPort { regSymbol: "reg_a", results: ["xmr_out"] }
// 生成：assign xmr_out = reg_a_val;  // 同上，然后连接到端口
```

---

## 三者的对齐对比

| 特性 | kMemory | kRegister | kLatch |
|------|---------|-----------|--------|
| **声明操作** | `kMemory` | `kRegister` | `kLatch` |
| **声明 attrs** | symbol, width, row, isSigned | symbol, width, isSigned | symbol, width, isSigned |
| **读端口** | `kMemoryReadPort` | `kRegisterReadPort` | `kLatchReadPort` |
| **读 operands** | addr | (无，标量) | (无，标量) |
| **读 results** | data | data | data |
| **读 attrs** | memSymbol | regSymbol | latchSymbol |
| **写端口** | `kMemoryWritePort` | `kRegisterWritePort` | `kLatchWritePort` |
| **写 operands** | updateCond, addr, data, mask, events... | updateCond, data, mask, events... | updateCond, data, mask |
| **写 attrs** | memSymbol, eventEdge[] | regSymbol, eventEdge[] | latchSymbol |
| **多写支持** | 是（多端口存储器） | **是（多 always 块）** | **否（单一端口）** |
| **触发方式** | 边沿触发 | 边沿触发 | 电平敏感 |
| **部分写入** | ✅（mask） | **✅（mask）** | **✅（mask）** |

---

## 升级计划（精简可执行）

### 0. 关键决策（已定）
- **兼容策略**：breaking，**不保留旧解析**，统一更新所有 fixture
- **XMR Write Op 升级**：XMR 写入必须携带触发事件信息（register/memory）或明确 latch；resolve **不再猜测时钟**，缺失即报错

### 1. 变更范围速查

| 层 | 主要文件 | 变更要点 |
|---|---|---|
| IR | `include/grh.hpp`, `src/grh.cpp` | 新 `OperationKind` + 解析/校验 |
| Convert | `src/convert.cpp` | 声明与读写拆分，单 always 合并为单 WritePort |
| Emit | `src/emit.cpp` | 新 Port emit，删除旧寄存器/锁存器路径 |
| Load/JSON | `src/load.cpp`, `docs/GRH-JSON-spec.md` | 新 kind 解析 + JSON 规范/示例 |
| Pass/XMR | `src/pass/*` | DCE/冗余/XMR resolve 走 Port，kXMRWrite 新字段 |
| Docs/Tests | `docs/GRH-representation.md`, `tests/*` | 文档同步与 fixture 更新 |

### 2. 分阶段执行（每阶段有出口标准）

**阶段 1：IR/JSON 基础**
- 交付：新 `OperationKind`、字符串解析、Load 校验、JSON 规范更新
- 出口：能 round-trip 新 kind 的 JSON；最小单元测试通过

**阶段 2：Convert**
- 交付：寄存器/锁存器拆分为声明 + ReadPort/WritePort；同一 always 合并为单 WritePort；多 always 支持
- 出口：`tests/convert/*` 覆盖单写与多写，fixture 全更新

**阶段 3：Emit + XMR**
- 交付：ReadPort 生成 `assign`，WritePort 生成 `always/always_latch`；XMR resolve 走 Port
- 出口：`tests/emit/*` 通过；典型样例 SV 与旧版本行为等价

**阶段 4：Passes 与回归**
- 交付：DCE/冗余/存储检查更新；必要时新增冲突检测
- 出口：`ctest` 全绿；可选 `make run_hdlbits_test` / C910 回归通过

### 3. 最小可交付路径（MVP）
按 **阶段 1 → 2 → 3** 顺序推进，阶段 4 在核心链路稳定后补齐。

---

## 附录：示例 JSON 结构

### 单写端口寄存器（完整写入）

```json
{
  "ops": [
    {
      "kind": "kRegister",
      "symbol": "reg_a",
      "attrs": { "width": 32, "isSigned": false }
    },
    {
      "kind": "kConstant",
      "results": ["mask_full"],
      "attrs": { "constValue": "32'hFFFFFFFF" }
    },
    {
      "kind": "kRegisterWritePort",
      "operands": ["update_cond", "next_val", "mask_full", "clk"],
      "attrs": {
        "regSymbol": "reg_a",
        "eventEdge": ["posedge"]
      }
    },
    {
      "kind": "kRegisterReadPort",
      "results": ["reg_a_q"],
      "attrs": { "regSymbol": "reg_a" }
    }
  ]
}
```

### 多写端口寄存器（完整写入 - 测试/功能模式）

```json
{
  "ops": [
    {
      "kind": "kRegister",
      "symbol": "mode_reg",
      "attrs": { "width": 8, "isSigned": false }
    },
    {
      "kind": "kConstant",
      "results": ["mask_full"],
      "attrs": { "constValue": "8'hFF" }
    },
    {
      "kind": "kRegisterWritePort",
      "operands": ["en1", "data1", "mask_full", "clk"],
      "attrs": {
        "regSymbol": "mode_reg",
        "eventEdge": ["posedge"]
      }
    },
    {
      "kind": "kRegisterWritePort",
      "operands": ["test_en", "test_data", "mask_full", "test_clk"],
      "attrs": {
        "regSymbol": "mode_reg",
        "eventEdge": ["posedge"]
      }
    },
    {
      "kind": "kRegisterReadPort",
      "results": ["mode_out"],
      "attrs": { "regSymbol": "mode_reg" }
    }
  ]
}
```

### 多写端口寄存器（部分写入 - 不同位段）

```json
{
  "ops": [
    {
      "kind": "kRegister",
      "symbol": "byte_reg",
      "attrs": { "width": 8, "isSigned": false }
    },
    {
      "kind": "kConstant",
      "results": ["mask_low", "mask_high"],
      "attrs": [
        { "constValue": "8'h0F" },
        { "constValue": "8'hF0" }
      ]
    },
    {
      "kind": "kRegisterWritePort",
      "operands": ["en1", "data1", "mask_low", "clk1"],
      "attrs": {
        "regSymbol": "byte_reg",
        "eventEdge": ["posedge"]
      }
    },
    {
      "kind": "kRegisterWritePort",
      "operands": ["en2", "data2", "mask_high", "clk2"],
      "attrs": {
        "regSymbol": "byte_reg",
        "eventEdge": ["posedge"]
      }
    },
    {
      "kind": "kRegisterReadPort",
      "results": ["byte_reg_q"],
      "attrs": { "regSymbol": "byte_reg" }
    }
  ]
}
```

---

## XMR 与 Port 模型的交互

XMR（跨模块引用）的 resolve pass 在处理存储单元时需要遵循 Port 模型的核心原则：**所有访问必须通过 Port**。

### XMR Read

当 XMR 读取目标模块中的存储单元时，resolve pass 应在**目标模块**创建对应的 ReadPort，然后将 ReadPort 的 result 提升为模块输出端口。

```
顶层模块 A
  └── 实例化 B (b1)
       └── 实例化 C (c1)
            └── kRegister { symbol: "reg_x" }

// A 中的 XMR 读取：b1.c1.reg_x
kXMRRead { xmrPath: "b1.c1.reg_x", results: ["val"] }
```

**Resolve 后**：

```
模块 C（新增）：
  - kRegister { symbol: "reg_x" }
  - kRegisterReadPort { regSymbol: "reg_x", results: ["reg_x_read"] }  ← 新建
  - outputPort { name: "reg_x_xmr_out", value: "reg_x_read" }          ← 提升为端口

模块 B（新增）：
  - inputPort { name: "reg_x_from_c", value: "..." }
  - outputPort { name: "reg_x_to_a", value: "..." }                    ← 透传

模块 A：
  - b1.reg_x 连接到 val
```

### XMR Write（升级要求）

XMR 写入存储单元时，resolve pass 应在**目标模块**创建新的 WritePort（而非 `kAssign`）。

**禁止**：~~`kAssign { output: "reg_x", input: xmr_data }`~~ ❌
（SV 语法错误：不能 continuous assign 到 reg）

**正确**：创建新的 WritePort ✅

```
// XMR 写入：b1.c1.reg_x <= xmr_data;

模块 C（resolve 后新增）：
  - kRegisterWritePort {
      operands: [xmr_en, xmr_data, mask_full, xmr_clk],
      attrs: { regSymbol: "reg_x", eventEdge: ["posedge"] }
    }
```

**升级要求**：`kXMRWrite` 必须携带**完整触发信息**，resolve pass **不再推断时钟**。
- 目标为 `kRegister/kMemory`：`kXMRWrite` 需提供事件列表（对应 `eventEdge` + 事件信号 operands）
- 目标为 `kLatch`：不需要事件列表，但必须保证写入语义为电平敏感（resolve 到 `kLatchWritePort`）
- 任一必需信息缺失 → **报错并拒绝展开**

### XMR 与存储类型对照表

| 目标存储类型 | XMR Read | XMR Write | 生成的 Port 类型 |
|-------------|---------|-----------|-----------------|
| `kRegister` | ✅ | ✅ | `kRegisterReadPort` / `kRegisterWritePort` |
| `kLatch` | ✅ | ✅ | `kLatchReadPort` / `kLatchWritePort` |
| `kMemory` | ✅ | ✅ | `kMemoryReadPort` / `kMemoryWritePort` |

### Transform Pass 更新

**XMR Resolve Pass** (`pass/xmr_resolve.cpp`) 需要更新：

- [ ] 读取目标为存储单元时，生成对应的 ReadPort 而非直接 symbol 引用
- [ ] 写入目标为存储单元时，生成对应的 WritePort 而非 `kAssign`
- [ ] 升级 `kXMRWrite` 字段，要求显式事件信息并在 resolve 中校验

---

## 相关文档

- `docs/GRH-representation.md`: 主 IR 文档（需要同步更新）
- `docs/GRH-JSON-spec.md`: JSON 规范（需要同步更新）
- `docs/convert/convert.md`: Convert 设计文档
- `docs/emit/`: Emit 相关文档
