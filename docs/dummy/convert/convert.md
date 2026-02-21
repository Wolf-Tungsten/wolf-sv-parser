# Convert 技术文档

> 本文档整合 Convert 的架构、工作流程与实现细节，对照 `lib/include/ingest.hpp` 与 `lib/src/ingest.cpp` 源码，以 Verilog 案例驱动方式讲解数据结构与算法流程。

---

## 目录

1. [概述](#1-概述)
2. [Pass 编号演进说明](#2-pass-编号演进说明)
3. [核心数据结构](#3-核心数据结构)
4. [算法流程](#4-算法流程)
5. [完整案例：从 Verilog 到 GRH](#5-完整案例从-verilog-到-grh)
6. [代码映射](#6-代码映射)
7. [并行化架构](#7-并行化架构)

---

## 1. 概述

### 1.1 目标与边界

Convert 将 slang AST（`slang::ast::*`）转换为 GRH IR（`wolvrix::lib::grh::Netlist` / `Graph` / `Value` / `Operation`）。

- **输入**：已解析并类型化的 slang AST（`Compilation` + `RootSymbol`）
- **输出**：结构化网表（`Netlist`），包含层次化的 `Graph` 表示
- **职责边界**：把静态 SystemVerilog 子集转为 SSA 形式的 GRH，保留层次、支持参数特化
- **非职责**：不覆盖所有 SV 特性，不做全局扁平化，不处理不可综合的动态语义

### 1.2 设计原则

| 原则 | 说明 |
|------|------|
| 分阶段 | 先收集与分析，再生成 IR，避免边扫边写 |
| 以 slang 类型系统为源 | 宽度、符号、packed/unpacked 维度从 `slang::ast::Type` 获取 |
| 显式数据模型 | 使用 Plan/Info 结构保存信号、端口与降级产物 |
| SSA 与四态 | GRH 以 SSA 表达，四态逻辑语义保持一致 |
| 模块粒度隔离 | 每个 `InstanceBodySymbol` 形成独立的 ModulePlan 与 Graph |
| 层次保留 | 实例关系通过 `kInstance` 维护，不做全局扁平化 |

---

## 2. Pass 编号说明

### 2.1 当前 Pass 架构（pass1-4）

| 新编号 | 名称 | 职责 | 对应旧编号 |
|--------|------|------|-----------|
| **Pass1** | ModulePlanner | 收集端口/信号/实例，解析类型 | 原 Pass1 |
| **Pass2** | StmtLowererPass | 表达式与语句降级，生成 Guard | 原 Pass5 |
| **Pass3** | WriteBackPass | 写回合并，生成 updateCond/nextValue | 原 Pass6 |
| **Pass4** | Assembly | Memory 端口处理 + GRH 图组装 | 原 Pass7 + 原 Pass8 |

### 2.2 旧编号历史（供 git 考古参考）

查看 `docs/convert/convert-progress.md` 的 STEP 0046~0048 了解历史合并详情：

| 旧编号 | 状态 | 演变 |
|--------|------|------|
| Pass1 | ✅ 保留 | 现为 **Pass1** |
| Pass2 | ❌ 已合并 | 合并入 Pass1（TypeResolver → ModulePlanner）|
| Pass3 | ❌ 已删除 | RWAnalyzer 被移除 |
| Pass4 | ❌ 已合并 | 合并入 Pass2（ExprLowerer → StmtLowerer）|
| Pass5 | ✅ 重编号 | 现为 **Pass2** |
| Pass6 | ✅ 重编号 | 现为 **Pass3** |
| Pass7 | ✅ 合并 | 并入 **Pass4**（MemoryPort → Assembly）|
| Pass8 | ✅ 合并 | 并入 **Pass4**（GraphAssembly → Assembly）|

---

## 3. 核心数据结构

本章采用**"定义详解 + 案例映射"**的方式讲解。先说明数据结构的字段含义，再用具体 Verilog 代码展示这些字段如何被填充。

### 示例 Verilog（贯穿本章）

```systemverilog
module adder_acc #(parameter WIDTH = 8)(
    input  logic clk,
    input  logic rst_n,
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    output logic [WIDTH:0]   sum,
    output logic [WIDTH-1:0] acc
);
    // 内部信号
    logic [WIDTH:0] temp_sum;
    logic [WIDTH-1:0] mem [0:3];
    
    // 组合逻辑
    assign temp_sum = a + b;
    assign sum = temp_sum;
    
    // 时序逻辑
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            acc <= '0;
        else
            acc <= acc + temp_sum[WIDTH-1:0];
    end
    
    // Memory 写入
    always_ff @(posedge clk)
        mem[0] <= acc;
endmodule
```

---

### 3.1 PlanKey / PlanEntry / PlanCache：模块标识与缓存

#### 3.1.1 PlanKey：模块的唯一标识

```cpp
// lib/include/ingest.hpp (line 494-505)
struct PlanKey {
    const slang::ast::DefinitionSymbol* definition = nullptr;
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string paramSignature;  // 参数序列化签名

    bool operator==(const PlanKey& other) const noexcept {
        if (definition || other.definition) {
            return definition == other.definition && 
                   paramSignature == other.paramSignature;
        }
        return body == other.body && paramSignature == other.paramSignature;
    }
};
```

**字段详解**：

| 字段 | 类型 | 含义 |
|------|------|------|
| `definition` | `DefinitionSymbol*` | 模块定义符号（如 `adder_acc` 的定义） |
| `body` | `InstanceBodySymbol*` | 实例体符号（参数特化后的实际体） |
| `paramSignature` | `string` | 非 localparam 参数的序列化签名（如 `"WIDTH=8"`） |

**作用**：
- 区分同一模块的不同参数特化版本
- 作为 `PlanCache` 的 key，实现模块级去重

**案例：参数特化的 key 生成**

当顶层模块实例化两次 `adder_acc`：

```systemverilog
adder_acc #(.WIDTH(8))  u1(...);   // 第一次实例化
adder_acc #(.WIDTH(16)) u2(...);   // 第二次实例化，不同参数
adder_acc #(.WIDTH(8))  u3(...);   // 第三次实例化，同 u1 参数
```

生成的三个 `PlanKey`：

| 实例 | definition | body | paramSignature | 结果 |
|------|-----------|------|----------------|------|
| u1 | `&adder_acc_def` | `&u1_body` | `"WIDTH=8"` | 新 key，需完整转换 |
| u2 | `&adder_acc_def` | `&u2_body` | `"WIDTH=16"` | 新 key，需完整转换 |
| u3 | `&adder_acc_def` | `&u3_body` | `"WIDTH=8"` | 与 u1 相同，直接复用缓存 |

**注意**：u1 和 u3 的 `body` 指针不同（不同实例），但 `paramSignature` 相同，因此复用同一 PlanEntry。

---

#### 3.1.2 PlanEntry：模块计划的生命周期容器

```cpp
// line 517-528
enum class PlanStatus { Pending, Planning, Done, Failed };

struct PlanEntry {
    PlanStatus status = PlanStatus::Pending;
    std::optional<ModulePlan> plan;      // Pass1 产物
    PlanArtifacts artifacts;              // Pass2~Pass4 中间产物
};

struct PlanArtifacts {
    std::optional<LoweringPlan> loweringPlan;    // Pass2 产物
    std::optional<WriteBackPlan> writeBackPlan;  // Pass3 产物
};
```

**字段详解**：

| 字段 | 类型 | 含义 |
|------|------|------|
| `status` | `PlanStatus` | 当前状态：Pending（待处理）、Planning（处理中）、Done（完成）、Failed（失败） |
| `plan` | `optional<ModulePlan>` | Pass1 生成的模块骨架（端口、信号、子实例） |
| `artifacts.loweringPlan` | `optional<LoweringPlan>` | Pass2 生成的表达式 DAG 和写回意图 |
| `artifacts.writeBackPlan` | `optional<WriteBackPlan>` | Pass3 生成的写回合并结果 |

**状态流转**：

```
初始:    {status=Pending, plan=nullopt, artifacts={}}
            ↓ tryClaim() 抢占处理权
处理中:  {status=Planning, plan=nullopt, artifacts={}}
            ↓ Pass1~Pass4 执行完成
完成:    {status=Done, plan=ModulePlan{...}, artifacts={loweringPlan=..., writeBackPlan=...}}
            ↓ 或某 Pass 出错
失败:    {status=Failed, plan=nullopt, artifacts={}}
```

**案例：u1 (WIDTH=8) 在 Context 中的状态变化**

以 `ConvertContext` 为根节点，展示 `adder_acc` 模块在转换过程中的数据填充：

```cpp
// ========== 初始状态（任务投递后）==========
ConvertContext {
    planCache: PlanCache {
        entries: {
            [PlanKey {                                    // key 1: WIDTH=8
                definition: &adder_acc_def,
                body: &u1_body,
                paramSignature: "WIDTH=8"
            }] => PlanEntry {
                status: Pending,                          // 待处理
                plan: nullopt,                            // 暂无
                artifacts: {}                             // 暂无
            }
            // ... 其他模块的 entry
        }
    },
    planQueue: [PlanKey{...u1...}],  // 待处理队列
    ...
}

// ========== Worker 线程抢占后 ==========
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...u1...}] => PlanEntry {
                status: Planning,                         // 处理中（互斥锁保护）
                plan: nullopt,
                artifacts: {}
            }
        }
    }
}

// ========== Pass1 完成后 ==========
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...u1...}] => PlanEntry {
                status: Planning,  // 仍在处理中
                plan: ModulePlan {                          // ← Pass1 产出
                    body: &u1_body,
                    symbolTable: {
                        storage_: ["adder_acc", "clk", "rst_n", "a", 
                                   "b", "sum", "acc", "temp_sum", "mem"],
                        index_: {"clk"→0, "rst_n"→1, ...}
                    },
                    moduleSymbol: PlanSymbolId{0},          // "adder_acc"
                    ports: [                                  // 6 个端口
                        PortInfo {symbol:"clk",   direction:Input,  width:1},
                        PortInfo {symbol:"rst_n", direction:Input,  width:1},
                        PortInfo {symbol:"a",     direction:Input,  width:8},
                        PortInfo {symbol:"b",     direction:Input,  width:8},
                        PortInfo {symbol:"sum",   direction:Output, width:9},
                        PortInfo {symbol:"acc",   direction:Output, width:8}
                    ],
                    signals: [                                // 2 个内部信号
                        SignalInfo {symbol:"temp_sum", kind:Variable, width:9},
                        SignalInfo {symbol:"mem", kind:Memory, width:8, 
                                    memoryRows:4, unpackedDims:[{4,0,3}]}
                    ],
                    instances: []                             // 无子实例
                },
                artifacts: {}
            }
        }
    }
}

// ========== Pass2 完成后 ==========
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...u1...}] => PlanEntry {
                status: Planning,
                plan: ModulePlan {...},
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {              // ← Pass2 产出
                        values: [                             // 表达式节点池
                            ExprNode {kind:Symbol, symbol:"a", widthHint:8},      // [0]
                            ExprNode {kind:Symbol, symbol:"b", widthHint:8},      // [1]
                            ExprNode {kind:Operation, op:kAdd, operands:[0,1],   // [2]
                                     tempSymbol:"_tmp0", widthHint:9},
                            // ... 更多节点（rst_n, acc, temp_sum[7:0] 等）
                        ],
                        writes: [                             // 写回意图
                            WriteIntent {                     // temp_sum = a + b
                                target:"temp_sum", value:2, guard:Invalid,
                                domain:Combinational
                            },
                            WriteIntent {                     // acc <= '0 (if 分支)
                                target:"acc", value:const_0, guard:!rst_n,
                                domain:Sequential, isNonBlocking:true
                            },
                            WriteIntent {                     // acc <= acc + ... (else)
                                target:"acc", value:add_expr, guard:rst_n,
                                domain:Sequential, isNonBlocking:true
                            },
                            WriteIntent {                     // mem[0] <= acc
                                target:"mem", slices:[BitSelect(0)], value:acc,
                                domain:Sequential, isNonBlocking:true
                            }
                        ],
                        loweredStmts: [                       // 带时序的语句
                            LoweredStmt {
                                kind:Write, write:writes[0],
                                eventEdges:[], eventOperands:[]
                            },
                            LoweredStmt {
                                kind:Write, write:writes[1],
                                eventEdges:[Posedge,Negedge],
                                eventOperands:[clk_id, rst_n_id]
                            },
                            LoweredStmt {
                                kind:Write, write:writes[2],
                                eventEdges:[Posedge,Negedge],
                                eventOperands:[clk_id, rst_n_id]
                            },
                            LoweredStmt {
                                kind:Write, write:writes[3],
                                eventEdges:[Posedge],
                                eventOperands:[clk_id]
                            }
                        ]
                    },
                    writeBackPlan: nullopt
                }
            }
        }
    }
}

// ========== Pass3 完成后 ==========
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...u1...}] => PlanEntry {
                status: Planning,
                plan: ModulePlan {...},
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {...},
                    writeBackPlan: WriteBackPlan {            // ← Pass3 产出
                        entries: [
                            Entry {                           // acc 的合并结果
                                target:"acc",
                                signal:SignalId{1},
                                domain:Sequential,
                                updateCond:const_1,           // 始终更新
                                nextValue:mux(!rst_n, 0, acc+temp_sum),
                                eventEdges:[Posedge,Negedge],
                                eventOperands:[clk_id, rst_n_id]
                            }
                            // mem[0] 的写回交给 Pass4 处理
                        ]
                    }
                }
            }
        }
    }
}

// ========== Pass4 完成后 ==========
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...u1...}] => PlanEntry {
                status: Done,                                 // ← 完成
                plan: ModulePlan {...},
                artifacts: PlanArtifacts {...}
            }
        }
    },
    // Netlist 已在 Pass4 中填充（通过 GraphAssembler）
}

---

#### 3.1.3 PlanCache：模块计划的并发缓存

```cpp
// line 530-557
class PlanCache {
public:
    bool tryClaim(const PlanKey& key);           // 抢占处理权
    void storePlan(const PlanKey& key, ModulePlan plan);  // 存储 Pass1 结果
    void markFailed(const PlanKey& key);
    std::optional<ModulePlan> findReady(const PlanKey& key) const;
    
    // Pass2~Pass4 产物读写
    bool setLoweringPlan(const PlanKey& key, LoweringPlan plan);
    bool setWriteBackPlan(const PlanKey& key, WriteBackPlan plan);
    std::optional<LoweringPlan> getLoweringPlan(const PlanKey& key) const;
    std::optional<WriteBackPlan> getWriteBackPlan(const PlanKey& key) const;

private:
    std::unordered_map<PlanKey, PlanEntry, PlanKeyHash> entries_;
    mutable std::mutex mutex_;
};
```

**核心能力**：

| 方法 | 作用 | 使用场景 |
|------|------|---------|
| `tryClaim(key)` | 原子性检查 key 是否存在，不存在则创建 Planning 状态的 entry | Worker 线程获取任务时调用，防止重复处理 |
| `storePlan(key, plan)` | 将 Pass1 的 ModulePlan 存入 entry | Pass1 完成后调用 |
| `setLoweringPlan / setWriteBackPlan` | 存储 Pass2/Pass3 产物 | 各 Pass 完成后调用 |
| `findReady(key)` | 查询已完成（Done 状态）的 ModulePlan | 父模块实例化子模块时，检查子模块是否已转换 |

**并发策略**：
- 读多写少场景：`mutex_` 保护整个 `entries_` 哈希表
- `tryClaim` 是核心同步点：确保同一 `PlanKey` 只被一个线程处理

---

### 3.2 ModulePlan：模块级静态计划

ModulePlan 是 **Pass1（ModulePlanner）的产物**，记录模块的**静态骨架信息**（不随运行变化的声明级信息）。

#### 3.2.1 数据结构定义

```cpp
// line 271-279
struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body = nullptr;  // 指向 slang AST
    PlanSymbolTable symbolTable;           // 模块内符号驻留表
    PlanSymbolId moduleSymbol;             // 模块名索引（含参数签名）
    std::vector<PortInfo> ports;           // 端口列表
    std::vector<SignalInfo> signals;       // 信号/变量/内存列表
    std::vector<InstanceInfo> instances;   // 子实例列表
    std::vector<InoutSignalInfo> inoutSignals;  // inout 信号展开
};
```

**字段详解**：

| 字段 | 类型 | 含义 |
|------|------|------|
| `body` | `InstanceBodySymbol*` | 指向 slang AST 的实例体，用于后续 Pass 遍历成员 |
| `symbolTable` | `PlanSymbolTable` | 模块内所有名称的驻留表（字符串 → PlanSymbolId） |
| `moduleSymbol` | `PlanSymbolId` | 模块名的符号索引（如 `"adder_acc"` 或 `"adder_acc_WIDTH_8"`） |
| `ports` | `vector<PortInfo>` | 模块端口列表，按声明顺序 |
| `signals` | `vector<SignalInfo>` | 内部信号（net/variable/memory），不含端口 |
| `instances` | `vector<InstanceInfo>` | 子模块实例列表，用于层次展开 |
| `inoutSignals` | `vector<InoutSignalInfo>` | inout 端口展开为 `__in/__out/__oe` 三信号 |

---

#### 3.2.2 PortInfo：端口信息

```cpp
// line 223-234
struct PortInfo {
    PlanSymbolId symbol;                    // 端口名符号
    PortDirection direction;                // Input/Output/Inout
    int32_t width = 0;                      // 位宽（从 Type 解析）
    bool isSigned = false;                  // 是否有符号
    struct InoutBinding {                   // inout 专用：展开为三信号
        PlanSymbolId inSymbol;              // xxx__in
        PlanSymbolId outSymbol;             // xxx__out
        PlanSymbolId oeSymbol;              // xxx__oe（output enable）
    };
    std::optional<InoutBinding> inoutSymbol;
};
```

**字段详解**：

| 字段 | 说明 |
|------|------|
| `symbol` | 端口名在 `symbolTable` 中的索引，如 `"clk"` `"rst_n"` |
| `direction` | 方向枚举：`Input`、`Output`、`Inout` |
| `width` | 位宽，由 Pass1 解析 `slang::ast::Type` 得到（如 `WIDTH=8` 时 `a` 的 width=8） |
| `isSigned` | 是否有符号（`logic signed` 或 `integer` 等） |
| `inoutSymbol` | inout 端口特有，展开为三个内部信号用于后续连接 |

**案例：adder_acc 的 ports 填充**

```systemverilog
module adder_acc #(parameter WIDTH = 8)(
    input  logic clk,                    // ports[0]
    input  logic rst_n,                  // ports[1]
    input  logic [WIDTH-1:0] a,          // ports[2]
    input  logic [WIDTH-1:0] b,          // ports[3]
    output logic [WIDTH:0]   sum,        // ports[4]
    output logic [WIDTH-1:0] acc         // ports[5]
);
```

当 `WIDTH=8` 时：

```cpp
ports = {
    [0] = {.symbol = "clk",   .direction = Input,  .width = 1,  .isSigned = false},
    [1] = {.symbol = "rst_n", .direction = Input,  .width = 1,  .isSigned = false},
    [2] = {.symbol = "a",     .direction = Input,  .width = 8,  .isSigned = false},
    [3] = {.symbol = "b",     .direction = Input,  .width = 8,  .isSigned = false},
    [4] = {.symbol = "sum",   .direction = Output, .width = 9,  .isSigned = false},  // WIDTH:0 = 9 位
    [5] = {.symbol = "acc",   .direction = Output, .width = 8,  .isSigned = false}
};
```

---

#### 3.2.3 SignalInfo：内部信号信息

```cpp
// line 247-255
struct SignalInfo {
    PlanSymbolId symbol;                    // 信号名符号
    SignalKind kind;                        // Net/Variable/Memory/Port
    int32_t width = 0;                      // 位宽
    bool isSigned = false;                  // 是否有符号
    int64_t memoryRows = 0;                 // Memory 行数（unpacked 维度乘积）
    std::vector<int32_t> packedDims;        // Packed 维度（外到内）
    std::vector<UnpackedDimInfo> unpackedDims;  // Unpacked 维度（外到内，含上下界）
};

// 辅助结构
struct UnpackedDimInfo {
    int32_t extent = 1;     // 维度大小（right-left+1 或 left-right+1）
    int32_t left = 0;       // 声明的左界（如 [7:0] 的 7）
    int32_t right = 0;      // 声明的右界（如 [7:0] 的 0）
};
```

**字段详解**：

| 字段 | 说明 |
|------|------|
| `kind` | 信号类型：`Net`（wire）、`Variable`（reg/logic）、`Memory`（数组）、`Port`（内部引用） |
| `width` | 基本位宽（不含 unpacked 维度） |
| `memoryRows` | 若为 Memory，是所有 unpacked 维度的乘积；普通信号为 0 |
| `packedDims` | Packed 数组维度，如 `logic [3:0][7:0]` → `{4, 8}` |
| `unpackedDims` | Unpacked 数组维度，含上下界信息，用于地址线性化 |

**案例：adder_acc 的 signals 填充**

```systemverilog
// 内部信号
logic [WIDTH:0] temp_sum;              // 普通变量
logic [WIDTH-1:0] mem [0:3];           // Memory（4 行 × 8 位）
```

当 `WIDTH=8` 时：

```cpp
signals = {
    // temp_sum：普通变量，9 位宽
    [0] = {
        .symbol = "temp_sum",
        .kind = Variable,
        .width = 9,              // WIDTH:0 = 9 位
        .isSigned = false,
        .memoryRows = 0,         // 不是 Memory
        .packedDims = {},
        .unpackedDims = {}
    },
    // mem：Memory，8 位宽，4 行
    [1] = {
        .symbol = "mem",
        .kind = Memory,
        .width = 8,              // 每行 8 位
        .isSigned = false,
        .memoryRows = 4,         // [0:3] = 4 行
        .packedDims = {},
        .unpackedDims = {
            {.extent = 4, .left = 0, .right = 3}  // [0:3]
        }
    }
};
```

**Memory 与普通变量的区别**：
- `temp_sum`：`kind=Variable`，可直接读写整个信号
- `mem`：`kind=Memory`，需要通过 `mem[addr]` 访问，Pass4 会生成 MemoryReadPort/MemoryWritePort

---

#### 3.2.4 InstanceInfo：子实例信息

```cpp
// line 262-269
struct InstanceInfo {
    const slang::ast::InstanceSymbol* instance = nullptr;  // 指向 slang 实例符号
    PlanSymbolId instanceSymbol;            // 实例名（如 "u1"）
    PlanSymbolId moduleSymbol;              // 模块定义名（如 "adder_acc"）
    bool isBlackbox = false;                // 是否黑盒（只有接口无实现）
    std::vector<InstanceParameter> parameters;  // 黑盒参数列表
    std::string paramSignature;             // 参数签名（用于生成 PlanKey）
};
```

**字段详解**：

| 字段 | 说明 |
|------|------|
| `instance` | 指向 slang AST 中的 `InstanceSymbol`，用于获取连接信息 |
| `instanceSymbol` | 实例化名（如 `"u1"` `"genblk1.inst"`） |
| `moduleSymbol` | 模块定义名（如 `"adder_acc"`） |
| `isBlackbox` | 若为 true，表示该模块只有接口声明，无内部实现（如第三方 IP） |
| `parameters` | 黑盒实例的参数名/值列表（普通实例在 Pass1 已特化，无需保留） |
| `paramSignature` | 参数签名字符串，用于生成子模块的 `PlanKey` |

**案例：顶层模块实例化 adder_acc**

```systemverilog
module top;
    logic clk, rst;
    logic [7:0] a, b, acc;
    logic [8:0] sum;
    
    adder_acc #(.WIDTH(8)) u1 (       // instances[0]
        .clk(clk), .rst_n(rst), .a(a), .b(b), .sum(sum), .acc(acc)
    );
endmodule
```

Pass1 处理 `top` 时生成的 InstanceInfo：

```cpp
// top 的 ModulePlan.instances
instances = {
    [0] = {
        .instance = &u1_symbol,           // slang 的 InstanceSymbol
        .instanceSymbol = "u1",           // 实例化名
        .moduleSymbol = "adder_acc",      // 模块定义名
        .isBlackbox = false,              // 有实现，非黑盒
        .parameters = {},                 // 非黑盒，参数已特化到 body 中
        .paramSignature = "WIDTH=8"       // 子模块的 PlanKey 组成部分
    }
};

// 同时，Pass1 会为 u1 创建 PlanKey 并投递到任务队列
PlanKey u1_key = {
    .definition = &adder_acc_def,
    .body = &u1_body,                     // 参数特化后的实例体
    .paramSignature = "WIDTH=8"
};
planQueue.push(u1_key);                   // 后续 Worker 线程会处理
```

---

#### 3.2.5 PlanSymbolTable：符号驻留表

```cpp
// line 139-149
class PlanSymbolTable {
public:
    PlanSymbolId intern(std::string_view text);    // 插入或查找
    PlanSymbolId lookup(std::string_view text) const;  // 仅查找
    std::string_view text(PlanSymbolId id) const;  // 反向查询
    std::size_t size() const noexcept;

private:
    std::deque<std::string> storage_;       // 实际存储字符串
    std::unordered_map<std::string_view, PlanSymbolId, StringViewHash, StringViewEq> index_;
};
```

**作用**：
- 将字符串名称（如 `"clk"` `"temp_sum"`）转换为整数 ID（`PlanSymbolId`）
- 避免重复存储相同字符串，节省内存
- 保证同一模块内相同名称指向同一 ID，便于比较

**案例：adder_acc 的符号驻留**

```cpp
// Pass1 调用 symbolTable.intern() 插入符号
symbolTable.intern("adder_acc");   // → PlanSymbolId{0}
symbolTable.intern("clk");          // → PlanSymbolId{1}
symbolTable.intern("rst_n");        // → PlanSymbolId{2}
symbolTable.intern("a");            // → PlanSymbolId{3}
symbolTable.intern("b");            // → PlanSymbolId{4}
symbolTable.intern("sum");          // → PlanSymbolId{5}
symbolTable.intern("acc");          // → PlanSymbolId{6}
symbolTable.intern("temp_sum");     // → PlanSymbolId{7}
symbolTable.intern("mem");          // → PlanSymbolId{8}

// 后续 Pass 通过 ID 引用，避免字符串比较
// 如 WriteIntent.target = PlanSymbolId{6} 表示写回 "acc"
```

---

### 3.3 LoweringPlan：表达式与语句降级结果

LoweringPlan 是 **Pass2（StmtLowererPass）的产物**，将 Verilog 的行为描述（always/assign/if/case）降级为**表达式 DAG** 和**写回意图**。

#### 3.3.1 数据结构定义

```cpp
// line 461-469
struct LoweringPlan {
    std::vector<ExprNode> values;              // 表达式 DAG 节点池
    std::vector<PlanSymbolId> tempSymbols;     // 操作节点的临时符号名
    std::vector<WriteIntent> writes;           // 原始写回意图（按出现顺序）
    std::vector<LoweredStmt> loweredStmts;     // 语句级结果（带时序/事件）
    std::vector<DpiImportInfo> dpiImports;     // DPI import 元数据
    std::vector<MemoryReadPort> memoryReads;   // Memory 读端口（Pass4 填充）
    std::vector<MemoryWritePort> memoryWrites; // Memory 写端口（Pass4 填充）
};
```

**字段详解**：

| 字段 | 说明 |
|------|------|
| `values` | 表达式节点池，通过索引（`ExprNodeId`）引用，形成 DAG |
| `tempSymbols` | 每个 Operation 节点分配一个临时符号名（如 `"_tmp0"`），用于调试 |
| `writes` | 所有赋值语句的写回意图（包括阻塞/非阻塞、连续赋值） |
| `loweredStmts` | 语句级视图，保留执行顺序和时序信息（`eventEdges`/`eventOperands`） |
| `dpiImports` | DPI-C 导入函数的签名信息 |
| `memoryReads/Writes` | Pass4 识别并填充的 Memory 端口信息 |

---

#### 3.3.2 ExprNode：表达式 DAG 节点

```cpp
// line 329-340
struct ExprNode {
    ExprNodeKind kind;                      // Constant/Symbol/XmrRead/Operation
    wolvrix::lib::grh::OperationKind op;              // 操作类型（仅 Operation 有效）
    PlanSymbolId symbol;                    // 命名值符号（Symbol 类型）
    PlanSymbolId tempSymbol;                // 临时符号（Operation 类型）
    std::string literal;                    // 常量文本（如 "8'b1010"）
    std::string xmrPath;                    // XMR 路径（跨模块引用）
    std::vector<ExprNodeId> operands;       // 操作数索引（指向 values 数组）
    int32_t widthHint = 0;                  // 表达式结果位宽提示
    bool isSigned = false;
    slang::SourceLocation location;         // 源码位置（用于诊断）
};

enum class ExprNodeKind { Invalid, Constant, Symbol, XmrRead, Operation };
```

**字段详解**：

| kind | 有效字段 | 含义 |
|------|---------|------|
| `Constant` | `literal`, `widthHint` | 常量值，如 `"4'b0"` `"8'hff"` |
| `Symbol` | `symbol` | 命名值引用，如 `acc` `temp_sum` |
| `XmrRead` | `xmrPath` | 跨模块引用，如 `"top.sub.sig"` |
| `Operation` | `op`, `operands`, `tempSymbol` | 运算操作，如 `kAdd` `kMux` |

**案例：`temp_sum = a + b` 在 LoweringPlan.values 中的表示**

```systemverilog
assign temp_sum = a + b;  // WIDTH=8，a/b 是 8 位，+ 的结果是 9 位（进位）
```

在 Context 中的结构：

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...adder_acc...}] => PlanEntry {
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {
                        values: [                           // 表达式节点池（索引即 ID）
                            // [0] Symbol: a
                            ExprNode {
                                kind: Symbol,
                                symbol: PlanSymbolId{3},    // 指向 "a"
                                widthHint: 8
                            },
                            // [1] Symbol: b
                            ExprNode {
                                kind: Symbol,
                                symbol: PlanSymbolId{4},    // 指向 "b"
                                widthHint: 8
                            },
                            // [2] Operation: a + b
                            ExprNode {
                                kind: Operation,
                                op: kAdd,
                                tempSymbol: PlanSymbolId{9},  // "_tmp0"
                                operands: [0, 1],              // ← 引用 values[0], values[1]
                                widthHint: 9                   // 进位结果 9 位
                            }
                            // ... 更多节点
                        ]
                    }
                }
            }
        }
    }
}
```

形成的 DAG 结构：
```
LoweringPlan.values:
  [0] Symbol("a", 8bit) ──┐
                          ├──[kAdd]──→ [2] Operation("_tmp0", 9bit)
  [1] Symbol("b", 8bit) ──┘
                               ↑
                               └─ 被 WriteIntent.value = 2 引用
```

---

#### 3.3.3 WriteIntent：写回意图

```cpp
// line 366-377
struct WriteIntent {
    PlanSymbolId target;                    // 写回目标符号
    std::vector<WriteSlice> slices;         // LHS 切片链路（如 mem[0].field）
    ExprNodeId value = kInvalidPlanIndex;   // RHS 根节点索引
    ExprNodeId guard = kInvalidPlanIndex;   // guard 条件（无效表示无条件）
    ControlDomain domain;                   // Comb/Sequential/Latch
    bool isNonBlocking = false;             // 是否 <= 赋值
    bool coversAllTwoState = false;         // case 二值完整覆盖标记
    bool isXmr = false;                     // 是否跨模块写
    std::string xmrPath;                    // XMR 路径
    slang::SourceLocation location;         // 源码位置
};

enum class ControlDomain { Combinational, Sequential, Latch, Unknown };
```

**字段详解**：

| 字段 | 说明 |
|------|------|
| `target` | 写回的目标信号名（如 `acc` `mem`） |
| `slices` | LHS 切片链，如 `mem[0]` 对应 BitSelect，`sig[7:4]` 对应 RangeSelect |
| `value` | RHS 表达式在 `values` 数组中的根节点索引 |
| `guard` | 条件 guard（如 `if (!rst_n)` 对应 `!rst_n` 节点），无条件时为 Invalid |
| `domain` | 控制域：组合逻辑、时序逻辑、锁存器 |
| `isNonBlocking` | 是否为 `<=` 赋值（影响语义，但不影响转换） |
| `coversAllTwoState` | case 语句在二值逻辑下是否完整覆盖（用于优化） |

**案例：`temp_sum = a + b` 的 WriteIntent**

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...adder_acc...}] => PlanEntry {
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {
                        values: [...],                        // 如上，[2] 是 kAdd
                        writes: [
                            WriteIntent {                     // ← 本条写回意图
                                target: PlanSymbolId{7},      // "temp_sum"
                                slices: [],                    // 空 = 无切片，整信号写入
                                value: ExprNodeId{2},          // ← 指向 values[2] (kAdd)
                                guard: kInvalidPlanIndex,      // 无效 = 无条件（assign）
                                domain: Combinational,
                                isNonBlocking: false
                            }
                        ]
                    }
                }
            }
        }
    }
}

**案例：`acc <= acc + temp_sum[7:0]` 的 WriteIntent（时序）**

```systemverilog
always_ff @(posedge clk or negedge rst_n)
    if (!rst_n)
        acc <= '0;
    else
        acc <= acc + temp_sum[WIDTH-1:0];
```

在 Context 中，假设 values 数组已构建：
- values[0]: Symbol `rst_n`
- values[1]: Operation `!rst_n` (guard for '0)
- values[2]: Constant `0`
- values[3]: Symbol `acc`
- values[4]: Symbol `temp_sum`
- values[5,6]: Constant `7,0` (range)
- values[7]: Operation `temp_sum[7:0]`
- values[8]: Operation `acc + temp_sum[7:0]`
- values[9]: guard for else branch

生成的 writes 数组：

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...adder_acc...}] => PlanEntry {
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {
                        values: [                           // 如上所述
                            ExprNode {/*[0] rst_n*/},
                            ExprNode {/*[1] !rst_n*/},
                            ExprNode {/*[2] 0*/},
                            // ... [3-8]
                            ExprNode {/*[9] rst_n guard*/}
                        ],
                        writes: [
                            // if (!rst_n) acc <= '0;
                            WriteIntent {
                                target: PlanSymbolId{6},      // "acc"
                                slices: [],
                                value: ExprNodeId{2},          // 指向 values[2] (0)
                                guard: ExprNodeId{1},          // 指向 values[1] (!rst_n)
                                domain: Sequential,
                                isNonBlocking: true
                            },
                            // else acc <= acc + temp_sum[7:0];
                            WriteIntent {
                                target: PlanSymbolId{6},      // "acc"
                                slices: [],
                                value: ExprNodeId{8},          // 指向 values[8] (加法)
                                guard: ExprNodeId{9},          // 指向 values[9] (rst_n)
                                domain: Sequential,
                                isNonBlocking: true
                            }
                        ]
                    }
                }
            }
        }
    }
}

---

#### 3.3.4 WriteSlice：LHS 切片描述

```cpp
// line 356-364
struct WriteSlice {
    WriteSliceKind kind;                // BitSelect/RangeSelect/MemberSelect
    WriteRangeKind rangeKind;           // Simple/IndexedUp/IndexedDown
    ExprNodeId index = kInvalidPlanIndex;    // bit-select 索引
    ExprNodeId left = kInvalidPlanIndex;     // range 左表达式
    ExprNodeId right = kInvalidPlanIndex;    // range 右表达式
    PlanSymbolId member;                // 成员名（结构体/联合体）
    slang::SourceLocation location;
};

enum class WriteSliceKind { None, BitSelect, RangeSelect, MemberSelect };
enum class WriteRangeKind { Simple, IndexedUp, IndexedDown };
```

**字段详解**：

| kind | 含义 | 对应 Verilog |
|------|------|-------------|
| `BitSelect` | 位选择 | `sig[3]` `mem[idx]` |
| `RangeSelect` | 范围选择 | `sig[7:4]` `sig[idx +: 2]` |
| `MemberSelect` | 成员选择 | `struct_var.field` |

| rangeKind | 含义 | 示例 |
|-----------|------|------|
| `Simple` | 常量范围 | `[7:4]` |
| `IndexedUp` | 基址+宽度向上 | `[base +: width]` |
| `IndexedDown` | 基址+宽度向下 | `[base -: width]` |

**案例：`mem[0] <= acc` 的切片**

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...adder_acc...}] => PlanEntry {
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {
                        writes: [
                            WriteIntent {
                                target: PlanSymbolId{8},      // "mem"
                                slices: [                      // ← 切片链
                                    WriteSlice {
                                        kind: BitSelect,
                                        index: ExprNodeId{...} // 指向常量 0
                                    }
                                ],
                                value: ExprNodeId{...},        // acc
                                domain: Sequential,
                                isNonBlocking: true
                            }
                        ]
                    }
                }
            }
        }
    }
}

**案例：`sig[idx +: 2]` 的切片**

```cpp
WriteSlice{
    .kind = RangeSelect,
    .rangeKind = IndexedUp,
    .left = ExprNodeId{...},          // idx（基址表达式）
    .right = ExprNodeId{...}          // 2（宽度）
}
```

---

#### 3.3.5 LoweredStmt：语句级降级结果

```cpp
// line 423-434
struct LoweredStmt {
    LoweredStmtKind kind;               // Write/SystemTask/DpiCall
    wolvrix::lib::grh::OperationKind op;          // 对应的 GRH 操作
    ExprNodeId updateCond;              // 触发条件（无条件时为常量 1）
    ProcKind procKind;                  // initial/final/always_*
    bool hasTiming;                     // 是否显式 timing control
    std::vector<EventEdge> eventEdges;  // posedge/negedge
    std::vector<ExprNodeId> eventOperands;  // 触发信号
    slang::SourceLocation location;
    WriteIntent write;                  // Write 语句内容
    SystemTaskStmt systemTask;          // system task 参数列表
    DpiCallStmt dpiCall;                // DPI 调用内容
};

enum class LoweredStmtKind { Write, SystemTask, DpiCall };
enum class EventEdge { Posedge, Negedge };
```

**与 WriteIntent 的区别**：
- `WriteIntent`：纯语义层面的"谁被赋了什么值"
- `LoweredStmt`：语句层面的"在什么条件下、什么时机执行"

**字段详解**：

| 字段 | 说明 |
|------|------|
| `kind` | 语句类型：赋值、system task、DPI 调用 |
| `updateCond` | 语句级触发条件（如 `$display` 的触发条件） |
| `procKind/hasTiming` | 过程块类型与 timing control 标记 |
| `eventEdges` | 边沿触发列表（如 `posedge clk` `negedge rst_n`） |
| `eventOperands` | 触发信号在 `values` 中的节点索引 |
| `write/systemTask/...` | union 风格的内容，根据 `kind` 选择有效字段 |

**案例：时序块的 LoweredStmt**

```systemverilog
always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        acc <= '0;
    else
        acc <= acc + temp_sum;
end
```

在 Context 中，生成的 loweredStmts：

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...adder_acc...}] => PlanEntry {
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {
                        values: [...],
                        writes: [...],
                        loweredStmts: [                       // ← 语句级视图
                            // 对应 acc <= '0
                            LoweredStmt {
                                kind: Write,
                                write: WriteIntent {
                                    target: "acc",
                                    value: ExprNodeId{...},   // 常量 0
                                    guard: ExprNodeId{...}    // !rst_n
                                },
                                eventEdges: [Posedge, Negedge],  // @(posedge clk or...)
                                eventOperands: [clk_id, rst_n_id]
                            },
                            // 对应 acc <= acc + temp_sum
                            LoweredStmt {
                                kind: Write,
                                write: WriteIntent {
                                    target: "acc",
                                    value: ExprNodeId{...},   // 加法结果
                                    guard: ExprNodeId{...}    // rst_n
                                },
                                eventEdges: [Posedge, Negedge],  // 相同的事件
                                eventOperands: [clk_id, rst_n_id]
                            }
                        ]
                    }
                }
            }
        }
    }
}
```

**注意**：同一个 always_ff 块内的多条语句共享相同的 `eventEdges`/`eventOperands`。

---

### 3.4 WriteBackPlan：写回合并结果

WriteBackPlan 是 **Pass3（WriteBackPass）的产物**，将 LoweringPlan 中同一目标的多个写回意图合并为单一的 `updateCond + nextValue`。

#### 3.4.1 数据结构定义

```cpp
// line 471-487
struct WriteBackPlan {
    struct Entry {
        PlanSymbolId target;                    // 写回目标
        SignalId signal = kInvalidPlanIndex;    // 目标信号在 signals 数组中的索引
        ControlDomain domain;                   // Comb/Sequential/Latch
        ExprNodeId updateCond;                  // 写回触发条件（OR 所有 guards）
        ExprNodeId nextValue;                   // 合并后的下一状态值
        bool hasStaticSlice = false;            // 是否含静态切片
        int64_t sliceLow = 0;                   // 切片起始位
        int64_t sliceWidth = 0;                 // 切片宽度
        std::vector<EventEdge> eventEdges;      // 时序域事件
        std::vector<ExprNodeId> eventOperands;  // 时序域触发信号
        slang::SourceLocation location;         // 首条写回位置
    };
    std::vector<Entry> entries;
};
```

**字段详解**：

| 字段 | 说明 |
|------|------|
| `target` | 目标信号名（如 `acc`） |
| `signal` | 在 `ModulePlan.signals` 中的索引，快速定位信号元数据 |
| `updateCond` | 所有 guard 的 OR，表示"何时需要更新"（时序域常为 1） |
| `nextValue` | 优先级 MUX 树，按语句顺序构建 |
| `hasStaticSlice` | 是否为部分位写回（需要 read-modify-write） |
| `eventEdges/Operands` | 时序域的触发信息（posedge clk 等） |

#### 3.4.2 合并规则

**分组键**：`target + domain + eventEdges + eventOperands`

同一组内的写回会被合并到一个 Entry。

**案例：acc 的写回合并**

输入（来自 LoweringPlan）：
1. `guard = !rst_n`, `value = 0`              // if (!rst_n)
2. `guard = rst_n`, `value = acc + temp_sum`  // else

合并过程：
```
updateCond = (!rst_n) OR (rst_n) = 1'b1  (始终更新)

nextValue 构建（先写的优先级高）：
    // 从最后一条开始倒序构建 MUX 链
    next = acc + temp_sum                    // 默认（else 分支）
    next = mux(!rst_n, 0, next)             // if (!rst_n) 优先级更高
    
最终结果: next = !rst_n ? 0 : (acc + temp_sum)
```

在 Context 中生成的 Entry：

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...adder_acc...}] => PlanEntry {
                plan: ModulePlan {
                    signals: [
                        SignalId{0}: ...,                          // temp_sum
                        SignalId{1}: {symbol:"acc", ...}           // acc 的索引
                    ]
                },
                artifacts: PlanArtifacts {
                    writeBackPlan: WriteBackPlan {
                        entries: [
                            Entry {                                 // acc 的合并结果
                                target: PlanSymbolId{6},           // "acc"
                                signal: SignalId{1},                // ← 指向 ModulePlan.signals[1]
                                domain: Sequential,
                                updateCond: kInvalidPlanIndex,      // 实际为常量 1（始终更新）
                                nextValue: ExprNodeId{...},         // mux(!rst_n, 0, acc+temp_sum)
                                hasStaticSlice: false,
                                eventEdges: [Posedge, Negedge],
                                eventOperands: [clk_id, rst_n_id]
                            }
                        ]
                    }
                }
            }
        }
    }
}
```

**案例：组合逻辑的写回**

```systemverilog
always_comb begin
    if (en) y = a;
    else    y = b;
end
```

在 Context 中：

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...}] => PlanEntry {
                artifacts: PlanArtifacts {
                    writeBackPlan: WriteBackPlan {
                        entries: [
                            Entry {
                                target: "y",
                                domain: Combinational,
                                updateCond: ExprNodeId{...},        // en || !en（始终为真）
                                nextValue: ExprNodeId{...},         // mux(en, a, b)
                                eventEdges: []
                            }
                        ]
                    }
                }
            }
        }
    }
}

---

### 3.5 数据结构关系总览（统一上下文视角）

以 `ConvertContext` 为根节点，展示 `adder_acc` 模块在各 Pass 后的完整数据状态：

```cpp
// ============================================================
// Verilog 源码
// ============================================================
// module adder_acc #(WIDTH=8)(
//     input  logic clk, rst_n,
//     input  logic [7:0] a, b,
//     output logic [8:0] sum,
//     output logic [7:0] acc
// );
//     logic [8:0] temp_sum;
//     logic [7:0] mem [0:3];
//     
//     assign temp_sum = a + b;
//     assign sum = temp_sum;
//     
//     always_ff @(posedge clk or negedge rst_n)
//         if (!rst_n) acc <= '0;
//         else        acc <= acc + temp_sum[7:0];
//     
//     always_ff @(posedge clk)
//         mem[0] <= acc;
// endmodule

// ============================================================
// Pass1 完成后：静态骨架信息
// ============================================================
ConvertContext {
    planCache: PlanCache {
        entries: {
            [PlanKey {definition:&adder_acc_def, body:&u1_body, 
                      paramSignature:"WIDTH=8"}] => PlanEntry {
                status: Planning,
                plan: ModulePlan {                          // ← Pass1 产出
                    body: &u1_body,
                    symbolTable: {
                        storage_: ["adder_acc", "clk", "rst_n", "a", "b", 
                                   "sum", "acc", "temp_sum", "mem"]
                        // index_: name -> PlanSymbolId 映射
                    },
                    moduleSymbol: PlanSymbolId{0},          // "adder_acc"
                    ports: [
                        PortInfo {symbol:"clk",   direction:Input,  width:1},
                        PortInfo {symbol:"rst_n", direction:Input,  width:1},
                        PortInfo {symbol:"a",     direction:Input,  width:8},
                        PortInfo {symbol:"b",     direction:Input,  width:8},
                        PortInfo {symbol:"sum",   direction:Output, width:9},
                        PortInfo {symbol:"acc",   direction:Output, width:8}
                    ],
                    signals: [
                        SignalInfo {symbol:"temp_sum", kind:Variable, width:9},
                        SignalInfo {symbol:"mem",      kind:Memory,   width:8, 
                                    memoryRows:4, unpackedDims:[{4,0,3}]}
                    ],
                    instances: [],                           // 无子实例
                    inoutSignals: []
                },
                artifacts: {}
            }
        }
    }
}

// ============================================================
// Pass2 完成后：表达式 DAG 与写回意图
// ============================================================
ConvertContext {
    planCache: {
        entries: {
            [...] => PlanEntry {
                status: Planning,
                plan: ModulePlan {...},                      // 同上
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {             // ← Pass2 产出
                        values: [                            // 表达式节点池
                            [0] ExprNode {kind:Symbol, symbol:"a"},
                            [1] ExprNode {kind:Symbol, symbol:"b"},
                            [2] ExprNode {kind:Operation, op:kAdd, 
                                         operands:[0,1]},   // a + b
                            [3] ExprNode {kind:Symbol, symbol:"rst_n"},
                            [4] ExprNode {kind:Operation, op:kLogicNot,
                                         operands:[3]},      // !rst_n
                            [5] ExprNode {kind:Constant, literal:"0"},
                            // ... 更多节点
                        ],
                        writes: [
                            WriteIntent {                     // temp_sum = a + b
                                target:"temp_sum", value:2, guard:Invalid,
                                domain:Combinational
                            },
                            WriteIntent {                     // acc <= '0
                                target:"acc", value:5, guard:4,
                                domain:Sequential, isNonBlocking:true
                            },
                            WriteIntent {                     // acc <= acc + ...
                                target:"acc", value:..., guard:3,
                                domain:Sequential, isNonBlocking:true
                            },
                            WriteIntent {                     // mem[0] <= acc
                                target:"mem", slices:[BitSelect{index:0}],
                                value:..., domain:Sequential
                            }
                        ],
                        loweredStmts: [
                            LoweredStmt {kind:Write, write:writes[0],
                                        eventEdges:[], eventOperands:[]},
                            LoweredStmt {kind:Write, write:writes[1],
                                        eventEdges:[Posedge,Negedge],
                                        eventOperands:[clk_id, rst_n_id]},
                            LoweredStmt {kind:Write, write:writes[2],
                                        eventEdges:[Posedge,Negedge],
                                        eventOperands:[clk_id, rst_n_id]},
                            LoweredStmt {kind:Write, write:writes[3],
                                        eventEdges:[Posedge],
                                        eventOperands:[clk_id]}
                        ]
                    }
                }
            }
        }
    }
}

// ============================================================
// Pass3 完成后：写回合并结果
// ============================================================
ConvertContext {
    planCache: {
        entries: {
            [...] => PlanEntry {
                plan: ModulePlan {...},
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {...},
                    writeBackPlan: WriteBackPlan {            // ← Pass3 产出
                        entries: [
                            Entry {                          // acc 的合并
                                target:"acc",
                                signal:SignalId{1},          // 指向 signals[1]
                                domain:Sequential,
                                updateCond:...,              // OR(guards) = 1
                                nextValue:...,               // mux(!rst_n, 0, acc+...)
                                eventEdges:[Posedge,Negedge],
                                eventOperands:[clk_id, rst_n_id]
                            }
                        ]
                    }
                }
            }
        }
    }
}

// ============================================================
// Pass4 完成后：GRH Graph 写入 Netlist
// ============================================================
ConvertContext {
    planCache: {
        entries: {
            [...] => PlanEntry {
                status: Done,                                // ← 完成
                ...
            }
        }
    }
    // Netlist 已由 GraphAssembler 填充：
    // Graph "adder_acc" {
    //     Input: clk, rst_n, a(8), b(8)
    //     Output: sum(9), acc(8)
    //     Ops: kAdd, kAssign, kRegister, kMemory, kMemoryWritePort, ...
    // }
}
```

---

## 4. 算法流程

### 4.1 ConvertDriver：入口与主流程

```cpp
// lib/include/ingest.hpp (line 639-654)
class ConvertDriver {
public:
    explicit ConvertDriver(ConvertOptions options = {});
    wolvrix::lib::grh::Netlist convert(const slang::ast::RootSymbol& root);
    // ...
};
```

**主流程**：

```cpp
wolvrix::lib::grh::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root) {
    wolvrix::lib::grh::Netlist netlist;
    planCache_.clear();
    planQueue_.reset();

    ConvertContext context{...};

    // 创建 Pass 实例
    ModulePlanner planner(context);
    StmtLowererPass stmtLowerer(context);
    WriteBackPass writeBack(context);
    MemoryPortLowererPass memoryPortLowerer(context);
    GraphAssembler graphAssembler(context, netlist);

    // 投递顶层实例任务
    for (const auto* topInstance : root.topInstances) {
        enqueuePlanKey(context, topInstance->body, paramSignature);
    }

    // 主循环：处理任务队列
    PlanKey key;
    while (planQueue_.tryPop(key)) {
        if (!planCache_.tryClaim(key)) continue;

        // Pass1: 模块计划
        ModulePlan plan = planner.plan(*key.body);
        
        // Pass2: 语句降级（含表达式）
        LoweringPlan lowering;
        stmtLowerer.lower(plan, lowering);
        
        // Pass3: 写回合并
        WriteBackPlan writeBackPlan = writeBack.lower(plan, lowering);
        
        // Pass4: Memory 端口 + 图组装
        memoryPortLowerer.lower(plan, lowering);
        wolvrix::lib::grh::Graph& graph = graphAssembler.build(key, plan, lowering, writeBackPlan);

        planCache_.storePlan(key, std::move(plan));
    }

    return netlist;
}
```

### 4.2 Pass1: ModulePlanner（符号收集 + 类型解析）

**职责**：构造 `ModulePlan`，填充 `symbolTable / ports / signals / instances`。

**核心流程**：
1. **collectPorts**：遍历 `body.getPortList()`，解析方向/位宽
2. **collectSignals**：扫描 `NetSymbol`/`VariableSymbol`，解析维度
3. **collectInstances**：扫描实例，投递子模块任务

### 4.3 Pass2: StmtLowererPass（表达式 + 语句降级）

**入口类型**：
- `ContinuousAssignSymbol` → 连续赋值
- `ProceduralBlockSymbol` → 过程块
- `GenerateBlockSymbol` → 生成器

**表达式降级**：
- `IntegerLiteral` → `ExprNode{kind=Constant}`
- `NamedValueExpression` → `ExprNode{kind=Symbol}`
- `BinaryExpression` → `ExprNode{kind=Operation, op=kAnd/...}`
- `ConditionalExpression` → `ExprNode{kind=Operation, op=kMux}`

**系统函数/任务支持现状（当前实现）**：

**表达式侧（system function）**：
- `$bits(expr)` → `kConstant`（常量值为位宽）
- `$signed(expr)` / `$unsigned(expr)` → `kAssign`（表达式 cast，保留值并标注 isSigned）
- `$time` / `$stime` / `$realtime` / `$random` / `$urandom` / `$urandom_range` / `$fopen` / `$ferror` / `$clog2` / `$size` → `kSystemFunction`
  - `$sformatf/$psprintf/$itor/$rtoi/$realtobits/$bitstoreal` → `kSystemFunction`
  - `$time/$stime/$realtime`：不接受参数
  - `$random/$urandom`：支持 0~1 个参数
  - `$urandom_range`：支持 1~2 个参数
  - `$fopen`：支持 1~2 个参数（文件名 + 可选模式）
  - `$ferror`：仅支持 1 个参数（文件句柄）；不支持输出字符串参数
  - `$itor/$rtoi/$realtobits/$bitstoreal`：仅支持 1 个参数
  - `$sformatf/$psprintf`：至少 1 个参数（format string + 可变参数）
  - `$clog2` / `$size`：参数可解析时在 convert 阶段折叠为 `kConstant`
  - `$random/$urandom/$urandom_range` 标注 `hasSideEffects`（避免折叠/去重）
  - `$fopen/$ferror` 标注 `hasSideEffects`（避免折叠/去重）

**语句侧（system task）**：
- `$display` / `$write` / `$strobe`
- `$fwrite` / `$fdisplay`（参数原样保留，首参通常为文件句柄）
- `$fclose`（要求文件句柄）
- `$info` / `$warning` / `$error` / `$fatal`
  - `$fatal`：参数原样保留（可选 exit code 作为首参）
- `$finish` / `$stop`（可选 exit code）
- `$dumpfile`（要求文件名参数）
- `$dumpvars`（仅支持全量 dump：无参数或 `0`）
- `$fflush`（可选文件句柄）
- `$readmemh/$readmemb`：记录为 `kMemory` 初始化属性（要求文件字面量 + memory 符号）
  - `start/finish` 仅支持常量
  - 非 `initial/final` 中出现会给 warning，并按初始化处理
- `$sdf_annotate/$monitor/$monitoron/$monitoroff`：warning 并忽略
  - 以上 system task 的参数均按调用顺序保留为 operands（含 format/file/exitCode）

**DPI import 函数**：
- 语句侧或表达式侧调用都降为 `LoweredStmtKind::DpiCall` + `kDpicCall`
- 仅支持 **function** + **input/output** 参数；`output` 通过临时 symbol 写回
- 表达式侧返回值为临时 symbol 的 `ExprNode{kind=Symbol}`

**统一建模现状（kSystemFunction / kSystemTask）**：
- `kSystemFunction` / `kSystemTask` 已成为 system call 的统一承载
- 旧 `kDisplay` / `kFwrite` / `kFinish` / `kAssert` 已移除，统一由 `kSystemTask` 表达
- `kSystemTask` 携带过程块上下文（`procKind`/`hasTiming`）以区分 initial/final/always 场景
- `kSystemTask` 的参数全部以 operands 表达（包含 format/file/exitCode）
- Transform 对 `kSystemFunction/kSystemTask` 视为有副作用节点，禁止常量折叠/去重

**控制流处理**：
- **if/else**：生成 `guard = base && cond`
- **case**：二值优先，四值回退
- **循环**：静态展开（上限 65536）

### 4.4 Pass3: WriteBackPass（写回合并）

**合并规则**：
1. **分组键**：`target + domain + eventEdges + eventOperands`
2. **Guard 合并**：`updateCond = OR(guard_i)`
3. **NextValue 合并**：`mux(guard_i, value_i, next)` 链

### 4.5 Pass4: Assembly（Memory 端口 + 图组装）

**MemoryPortLowerer**：
- 识别 `mem[addr]` 访问
- 生成 `MemoryReadPort` / `MemoryWritePort`
- 多维地址线性化

**GraphAssembler**：
- 创建端口/信号 Value
- 发射表达式节点
- 生成 `kRegister` / `kAssign` / `kMemory` / `kInstance`

---

## 5. 完整案例：从 Verilog 到 GRH

### 5.1 输入：带复位和使能的计数器

```systemverilog
module counter (
    input  logic clk,
    input  logic rst,
    input  logic en,
    output logic [3:0] count
);
    always_ff @(posedge clk) begin
        if (rst)
            count <= 4'b0;
        else if (en)
            count <= count + 4'b1;
    end
endmodule
```

### 5.2 Pass1 后：ModulePlan

```cpp
ConvertContext {
    planCache: {
        entries: {
            [PlanKey{...counter...}] => PlanEntry {
                plan: ModulePlan {
                    moduleSymbol: "counter",
                    ports: [
                        PortInfo {symbol:"clk",   direction:Input,  width:1},
                        PortInfo {symbol:"rst",   direction:Input,  width:1},
                        PortInfo {symbol:"en",    direction:Input,  width:1},
                        PortInfo {symbol:"count", direction:Output, width:4}
                    ],
                    signals: [
                        SignalInfo {symbol:"count", kind:Variable, width:4}
                    ]
                }
            }
        }
    }
}
```

### 5.3 Pass2 后：LoweringPlan

```cpp
ConvertContext {
    planCache: {
        entries: {
            [...] => PlanEntry {
                artifacts: PlanArtifacts {
                    loweringPlan: LoweringPlan {
                        values: [
                            [0] Symbol("rst"),
                            [1] Symbol("en"),
                            [2] Symbol("count"),
                            [3] Constant("4'b0"),
                            [4] Constant("4'b1"),
                            [5] Op(kAdd, [2,4]),      // count + 1
                            [6] Op(kLogicNot, [0]),   // !rst
                            [7] Op(kLogicAnd, [6,1])  // !rst && en
                        ],
                        writes: [
                            WriteIntent {target:"count", value:3, guard:0, domain:Seq},
                            WriteIntent {target:"count", value:5, guard:7, domain:Seq}
                        ],
                        loweredStmts: [
                            LoweredStmt {write:writes[0], eventEdges:[Posedge]},
                            LoweredStmt {write:writes[1], eventEdges:[Posedge]}
                        ]
                    }
                }
            }
        }
    }
}
```

### 5.4 Pass3 后：WriteBackPlan

```cpp
ConvertContext {
    planCache: {
        entries: {
            [...] => PlanEntry {
                artifacts: PlanArtifacts {
                    writeBackPlan: WriteBackPlan {
                        entries: [
                            Entry {
                                target:"count",
                                updateCond:...,      // rst || (!rst && en)
                                nextValue:...,       // mux(!rst && en, count+1, 0)
                                eventEdges:[Posedge]
                            }
                        ]
                    }
                }
            }
        }
    }
}
```

### 5.5 Pass4 后：GRH Graph

```cpp
ConvertContext {
    planCache: {
        entries: {
            [...] => PlanEntry {
                status: Done
            }
        }
    }
    // Netlist 已填充：
    // Graph "counter" {
    //     Input: clk, rst, en
    //     Output: count(4)
    //     Op: kRegister(updateCond, nextValue, clk) -> count
    // }
}
```

---

## 6. 代码映射

### 6.1 文件位置

| 文件 | 说明 |
|------|------|
| `lib/include/ingest.hpp` | 数据结构定义（~658 行） |
| `lib/src/ingest.cpp` | Pass 实现（~12000 行） |
| `tests/ingest/data/*.sv` | 测试 fixture |

### 6.2 关键定义

| 名称 | 行号 | 功能 |
|------|------|------|
| `ConvertContext` | 185 | 共享状态容器 |
| `ModulePlan` | 271 | 模块骨架 |
| `LoweringPlan` | 461 | 降级计划 |
| `WriteBackPlan` | 471 | 写回合并结果 |
| `ConvertDriver` | 639 | 入口类 |

### 6.3 函数入口

```cpp
ModulePlan ModulePlanner::plan(const slang::ast::InstanceBodySymbol& body);          // ~6808
void StmtLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering);              // ~6825
WriteBackPlan WriteBackPass::lower(ModulePlan& plan, LoweringPlan& lowering);       // ~7026
void MemoryPortLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering);        // ~8721
wolvrix::lib::grh::Graph& GraphAssembler::build(const PlanKey& key, ...);                     // ~11804
wolvrix::lib::grh::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root);        // ~11833
```

---

## 7. 并行化架构

### 7.1 设计要点

- **并行粒度**：Graph 创建级别
- **线程池**：默认 32 线程
- **去重**：`InstanceRegistry` 基于 `PlanKey`
- **Netlist 写回**：主线程串行 commit

### 7.2 执行流程

```
主线程
  ├── 投递顶层任务
  ├── 启动 Worker 线程池
  └── 等待完成 ←── Worker 线程（处理各模块 Pass1-4）
```

### 7.3 诊断与日志

- **诊断**：线程本地缓冲 + 统一 flush
- **耗时日志**：`pass1-module-plan` / `pass2-stmt-lowerer` / `pass3-writeback` / `pass4-assembly`

---

## 附录：术语表

| 术语 | 说明 |
|------|------|
| GRH | Graph-based Hardware IR |
| Guard | 控制流条件的逻辑累积 |
| Lowering | 从高阶表示降级到低阶表示 |
| Plan | 转换过程中的中间数据结构 |
| Pass | 对数据结构的一次遍历处理 |
| slang | SystemVerilog 前端解析器 |
| SSA | Static Single Assignment |
| PlanKey | 模块实例的唯一标识（body + paramSignature） |
| ControlDomain | 控制域：Combinational/Sequential/Latch |
