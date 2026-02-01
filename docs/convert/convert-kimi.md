# Convert 流程技术文档

本文档按"先数据结构、后算法"的思路组织，帮助理解 Convert 如何将 SystemVerilog 转换为 GRH 表示。

---

## 目录

1. 数据结构描述规范
2. 数据结构（静态骨架）
   - 2.1 ConvertContext：共享状态
   - 2.2 PlanCache、PlanKey 与 PlanEntry
   - 2.3 ModulePlan：模块骨架
   - 2.4 LoweringPlan：降级计划
   - 2.5 WriteIntent / WriteSlice：写回意图
   - 2.6 WriteBackPlan：写回合并结果
3. 算法流程（动态处理）
   - 3.1 ConvertDriver：入口与主流程
   - 3.2 Pass1：符号收集
   - 3.3 Pass2：类型解析
   - 3.4 Pass3：读写分析
   - 3.5 Pass4：表达式降级
   - 3.6 Pass5：语句降级
   - 3.7 Pass6：写回合并
   - 3.8 Pass7：内存端口处理
   - 3.9 Pass8：图组装
4. 完整示例
   - 4.1 示例源码
   - 4.2 各阶段数据结构状态
5. 代码映射

---

## 1. 数据结构描述规范

为保持文档一致性，本文档采用以下规范描述数据结构：

### 1.1 数组描述

数组使用下标索引表示，格式为 `array[index]`：

```cpp
// 数组定义
std::vector<ExprNode> values;

// 描述方式
values[0] = ExprNode{kind=Symbol, symbol="a"}
values[1] = ExprNode{kind=Symbol, symbol="b"}
values[2] = ExprNode{kind=Operation, op=kAnd}

// 或简写为
values = {
    [0] = ExprNode{kind=Symbol, symbol="a"},
    [1] = ExprNode{kind=Symbol, symbol="b"},
    [2] = ExprNode{kind=Operation, op=kAnd}
}
```

### 1.2 结构体描述

结构体使用字段访问表示，格式为 `struct.field = value`：

```cpp
// 结构体定义
struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body;
    std::vector<PortInfo> ports;
};

// 描述方式
plan.body = &counter_body
plan.ports[0].symbol = "clk"
plan.ports[0].direction = Input

// 或整体描述为
plan = {
    body = &counter_body,
    ports = {
        [0] = {symbol="clk", direction=Input, width=1},
        [1] = {symbol="rst", direction=Input, width=1}
    }
}
```

### 1.3 指针与引用

- 指针：`ptr = &object` 或 `ptr -> field`（表示指向关系）
- 空指针：`ptr = nullptr`
- 可选类型：`field = optional(value)` 或 `field = nullopt`

### 1.4 枚举值

枚举值直接使用枚举名，不加作用域：

```cpp
// 描述方式
domain = Sequential
kind = BitSelect
rangeKind = IndexedUp
```

### 1.5 PlanEntry 作为根节点

从 2.2 节引入 PlanEntry 后，所有后续案例均以 PlanEntry 为数据结构起点，展示其内部嵌套结构：

```cpp
// 标准描述形式
PlanEntry entry = {
    status = Done,
    plan = ModulePlan{
        body = &counter_body,
        ports = {...},
        signals = {...}
    },
    artifacts = PlanArtifacts{
        loweringPlan = LoweringPlan{
            values = {...},
            writes = {...}
        },
        writeBackPlan = WriteBackPlan{
            entries = {...}
        }
    }
}
```

### 1.6 简化表示

当某些字段与当前讨论无关时，使用 `...` 或 `/* ... */` 省略：

```cpp
entry = {
    status = Done,
    plan = ModulePlan{
        /* 其他字段省略 */
        ports = {...}
    },
    artifacts = {...}
}
```

---

## 2. 数据结构（静态骨架）

数据结构是 Convert 流程的静态骨架，保存从 AST 提取的各种信息。算法（Pass）读取、转换、写入这些数据结构，逐步将 SystemVerilog 转换为 GRH。

### 2.1 ConvertContext：共享状态

ConvertContext 是贯穿转换流程的共享状态容器，为 Pass1-Pass8 提供编译期信息与缓存/队列入口。定义在 `include/convert.hpp` 第 166-174 行：

```cpp
struct ConvertContext {
    const slang::ast::Compilation* compilation = nullptr;
    const slang::ast::RootSymbol* root = nullptr;
    ConvertOptions options{};
    ConvertDiagnostics* diagnostics = nullptr;
    ConvertLogger* logger = nullptr;
    PlanCache* planCache = nullptr;
    PlanTaskQueue* planQueue = nullptr;
};
```

**字段说明**：
- `compilation`：指向 slang Compilation 对象，访问全局类型/语义信息
- `root`：指向 RootSymbol，访问顶层实例列表
- `options`：转换选项值拷贝（abortOnError、maxLoopIterations 等）
- `diagnostics`：指向 ConvertDiagnostics，收集诊断信息
- `logger`：指向 ConvertLogger，记录日志
- `planCache`：指向 PlanCache，存储 PlanEntry
- `planQueue`：指向 PlanTaskQueue，调度待处理模块

**例子**：ConvertContext 构建

```cpp
ctx = {
    compilation = &root.getCompilation(),
    root = &root,
    options = ConvertOptions{abortOnError=true, maxLoopIterations=65536},
    diagnostics = &driver.diagnostics_,
    logger = &driver.logger_,
    planCache = &driver.planCache_,
    planQueue = &driver.planQueue_
}
```

生命周期：在 ConvertDriver::convert 中栈上临时构造，传递给各 Pass。

---

### 2.2 PlanCache、PlanKey 与 PlanEntry

这三个概念共同构成模块计划的存储与索引体系。

**PlanCache** 是全局缓存，存储所有模块的计划数据。它内部维护一个哈希表，定义在 `include/convert.hpp` 第 525-552 行：

```cpp
class PlanCache {
private:
    mutable std::mutex mutex_;
    std::unordered_map<PlanKey, PlanEntry, PlanKeyHash> entries_;
};
```

**PlanKey** 是哈希表的键（第 489-510 行），用于唯一标识和查找模块实例：

```cpp
struct PlanKey {
    const slang::ast::DefinitionSymbol* definition = nullptr;
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string paramSignature;
};
```

相同 PlanKey 的模块只处理一次，实现去重。

**PlanEntry** 是哈希表的值（第 519-523 行），实际存储在 PlanCache 中，包含一个模块从计划到产物的完整生命周期：

```cpp
struct PlanEntry {
    PlanStatus status = PlanStatus::Pending;
    std::optional<ModulePlan> plan;
    PlanArtifacts artifacts;
};
```

**字段说明**：
- `status`：处理状态（Pending / Planning / Done / Failed）
- `plan`：模块计划（ModulePlan），可选
- `artifacts`：阶段性产物（LoweringPlan / WriteBackPlan）

**例子**：PlanEntry 状态转换

```cpp
// 初始状态
PlanEntry entry = {
    status = Pending,
    plan = nullopt,
    artifacts = {
        loweringPlan = nullopt,
        writeBackPlan = nullopt
    }
}

// Pass1 后（SymbolCollector）
entry.status = Planning
entry.plan = ModulePlan{
    body = &adder_body,
    symbolTable = {"a", "b", "y"},
    moduleSymbol = "adder",
    ports = {
        [0] = {symbol="a", direction=Input, width=0},
        [1] = {symbol="b", direction=Input, width=0},
        [2] = {symbol="y", direction=Output, width=0}
    },
    signals = {},
    rwOps = {},
    memPorts = {},
    instances = {}
}

// Pass2-3 后（TypeResolver / RWAnalyzer）
entry.plan.ports[0].width = 8
entry.plan.ports[1].width = 8
entry.plan.ports[2].width = 8
entry.plan.rwOps = {
    [0] = {target="a", domain=Combinational, isWrite=false},
    [1] = {target="y", domain=Combinational, isWrite=true}
}

// Pass4-5 后（ExprLowerer / StmtLowerer）
entry.status = Done
entry.artifacts.loweringPlan = LoweringPlan{
    values = {
        [0] = {kind=Symbol, symbol="a"},
        [1] = {kind=Symbol, symbol="b"},
        [2] = {kind=Operation, op=kAdd, operands={0,1}}
    },
    roots = {[0]={value=2}},
    writes = {
        [0] = {target="y", value=2, guard=kInvalidPlanIndex}
    }
}

// Pass6-7 后（WriteBack / MemoryPortLowerer）
entry.artifacts.writeBackPlan = WriteBackPlan{
    entries = {
        [0] = {target="y", domain=Combinational, updateCond=kInvalidPlanIndex, nextValue=2}
    }
}
```

**三者的关系**：

```
PlanCache (ConvertDriver 成员)
    └── entries_ (unordered_map)
            ├── PlanKey{adder_def, "WIDTH=8"}  →  PlanEntry{plan=adder_8bit, ...}
            ├── PlanKey{adder_def, "WIDTH=16"} →  PlanEntry{plan=adder_16bit, ...}
            └── PlanKey{top_def, ""}           →  PlanEntry{plan=top, ...}
```

---

### 2.3 ModulePlan：模块骨架

ModulePlan 是模块级的静态计划，保存端口、信号、实例、读写关系等长期稳定事实。定义在 `include/convert.hpp` 第 268-277 行：

```cpp
struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    PlanSymbolTable symbolTable;
    PlanSymbolId moduleSymbol;
    std::vector<PortInfo> ports;
    std::vector<SignalInfo> signals;
    std::vector<RWOp> rwOps;
    std::vector<MemoryPortInfo> memPorts;
    std::vector<InstanceInfo> instances;
};
```

**字段说明**：
- `body`：指向 InstanceBodySymbol（AST 入口）
- `symbolTable`：符号驻留表（string -> PlanSymbolId）
- `moduleSymbol`：模块名索引（PlanSymbolId）
- `ports`：端口信息列表（PortInfo 数组）
- `signals`：信号信息列表（SignalInfo 数组）
- `rwOps`：读写访问记录（RWOp 数组）
- `memPorts`：内存端口记录（MemoryPortInfo 数组）
- `instances`：子实例记录（InstanceInfo 数组）

**例子**：端口收集（以 PlanEntry 为起点）

SystemVerilog 源码：

```systemverilog
module test (
    input  logic [7:0] a,
    output logic [7:0] y
);
```

Pass1 执行后：

```cpp
PlanEntry entry = {
    status = Done,
    plan = {
        body = &test_body,
        symbolTable = {"a" -> id(0), "y" -> id(1)},
        moduleSymbol = id(0),
        ports = {
            [0] = {
                symbol = id(0),
                direction = Input,
                width = 0,
                isSigned = false
            },
            [1] = {
                symbol = id(1),
                direction = Output,
                width = 0,
                isSigned = false
            }
        },
        signals = {},
        rwOps = {},
        memPorts = {},
        instances = {}
    },
    artifacts = {}
}
```

Pass2 执行后（类型填充）：

```cpp
entry.plan.ports[0].width = 8
entry.plan.ports[1].width = 8
```

---

### 2.4 LoweringPlan：降级计划

LoweringPlan 记录 Pass4-Pass7 的降级结果，包含表达式节点图、写回意图、内存端口等。定义在 `include/convert.hpp` 第 458-467 行：

```cpp
struct LoweringPlan {
    std::vector<ExprNode> values;
    std::vector<LoweredRoot> roots;
    std::vector<PlanSymbolId> tempSymbols;
    std::vector<WriteIntent> writes;
    std::vector<LoweredStmt> loweredStmts;
    std::vector<DpiImportInfo> dpiImports;
    std::vector<MemoryReadPort> memoryReads;
    std::vector<MemoryWritePort> memoryWrites;
};
```

**字段说明**：
- `values`：表达式节点序列（ExprNode 数组）
- `roots`：每条 RHS 的根节点入口（LoweredRoot 数组）
- `tempSymbols`：操作节点的临时符号名（PlanSymbolId 数组）
- `writes`：写回意图列表（WriteIntent 数组）
- `loweredStmts`：按序语句列表（LoweredStmt 数组）
- `dpiImports`：DPI import 元数据（DpiImportInfo 数组）
- `memoryReads`：读端口条目（MemoryReadPort 数组）
- `memoryWrites`：写端口条目（MemoryWritePort 数组）

**例子**：表达式降级（以 PlanEntry 为起点）

SystemVerilog 源码：

```systemverilog
assign y = (a & b) ? ~c : (a | b);
```

Pass4 执行后：

```cpp
PlanEntry entry = {
    status = Done,
    plan = { /* ... */ },
    artifacts = {
        loweringPlan = {
            values = {
                [0] = {kind=Symbol, symbol="a"},
                [1] = {kind=Symbol, symbol="b"},
                [2] = {kind=Operation, op=kAnd, operands={0,1}, tempSymbol="_expr_tmp_0"},
                [3] = {kind=Symbol, symbol="c"},
                [4] = {kind=Operation, op=kNot, operands={3}, tempSymbol="_expr_tmp_1"},
                [5] = {kind=Symbol, symbol="a"},
                [6] = {kind=Symbol, symbol="b"},
                [7] = {kind=Operation, op=kOr, operands={5,6}, tempSymbol="_expr_tmp_2"},
                [8] = {kind=Operation, op=kMux, operands={2,4,7}, tempSymbol="_expr_tmp_3"}
            },
            roots = {
                [0] = {value=8, location=@line1}
            },
            tempSymbols = {
                [0] = "_expr_tmp_0",
                [1] = "_expr_tmp_1",
                [2] = "_expr_tmp_2",
                [3] = "_expr_tmp_3"
            },
            writes = {},
            loweredStmts = {},
            dpiImports = {},
            memoryReads = {},
            memoryWrites = {}
        },
        writeBackPlan = nullopt
    }
}
```

---

### 2.5 WriteIntent / WriteSlice：写回意图

WriteIntent 描述单次赋值操作的完整信息。定义在 `include/convert.hpp` 第 365-374 行：

```cpp
struct WriteIntent {
    PlanSymbolId target;
    std::vector<WriteSlice> slices;
    ExprNodeId value = kInvalidPlanIndex;
    ExprNodeId guard = kInvalidPlanIndex;
    ControlDomain domain = ControlDomain::Unknown;
    bool isNonBlocking = false;
    bool coversAllTwoState = false;
    slang::SourceLocation location{};
};
```

**字段说明**：
- `target`：写回目标符号（PlanSymbolId）
- `slices`：LHS 切片链路（WriteSlice 数组）
- `value`：RHS 值节点索引（ExprNodeId）
- `guard`：guard 条件节点索引（ExprNodeId）
- `domain`：控制域（Combinational / Sequential / Latch / Unknown）
- `isNonBlocking`：是否非阻塞赋值
- `coversAllTwoState`：二值覆盖完整标记

**WriteSlice**：描述单次切片操作，定义在第 355-363 行

```cpp
struct WriteSlice {
    WriteSliceKind kind = WriteSliceKind::None;
    WriteRangeKind rangeKind = WriteRangeKind::Simple;
    ExprNodeId index = kInvalidPlanIndex;
    ExprNodeId left = kInvalidPlanIndex;
    ExprNodeId right = kInvalidPlanIndex;
    PlanSymbolId member;
    slang::SourceLocation location{};
};
```

**字段说明**：
- `kind`：切片类型（None / BitSelect / RangeSelect / MemberSelect）
- `rangeKind`：范围选择子类型（Simple / IndexedUp / IndexedDown）
- `index`：bit-select 索引（ExprNodeId）
- `left` / `right`：range 左右边界（ExprNodeId）
- `member`：成员名（PlanSymbolId，仅 MemberSelect 有效）

**例子**：位选择与范围选择（以 PlanEntry 为起点）

SystemVerilog 源码：

```systemverilog
always_comb begin
    y[3] = a;
    y[7:4] = b;
    y[idx +: 2] = c;
end
```

Pass5 执行后：

```cpp
PlanEntry entry = {
    status = Done,
    plan = { /* ... */ },
    artifacts = {
        loweringPlan = {
            values = {
                [0] = {kind=Symbol, symbol="a"},
                [1] = {kind=Symbol, symbol="b"},
                [2] = {kind=Symbol, symbol="c"},
                [3] = {kind=Symbol, symbol="idx"},
                [4] = {kind=Constant, literal="3"},
                [5] = {kind=Constant, literal="7"},
                [6] = {kind=Constant, literal="4"},
                [7] = {kind=Constant, literal="2"}
            },
            writes = {
                [0] = {
                    target = "y",
                    slices = {
                        [0] = {kind=BitSelect, index=4}
                    },
                    value = 0,
                    guard = kInvalidPlanIndex,
                    domain = Combinational
                },
                [1] = {
                    target = "y",
                    slices = {
                        [0] = {kind=RangeSelect, rangeKind=Simple, left=5, right=6}
                    },
                    value = 1,
                    guard = kInvalidPlanIndex,
                    domain = Combinational
                },
                [2] = {
                    target = "y",
                    slices = {
                        [0] = {kind=RangeSelect, rangeKind=IndexedUp, index=3, right=7}
                    },
                    value = 2,
                    guard = kInvalidPlanIndex,
                    domain = Combinational
                }
            }
        }
    }
}
```

**例子**：嵌套切片（以 PlanEntry 为起点）

SystemVerilog 源码：

```systemverilog
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
pair_t mem [0:3];

always_ff @(posedge clk) begin
    mem[idx].hi <= a;
end
```

生成的 WriteIntent：

```cpp
writes[0] = {
    target = "mem",
    slices = {
        [0] = {kind=BitSelect, index=ExprNodeId{"idx"}},
        [1] = {kind=MemberSelect, member="hi"}
    },
    value = ExprNodeId{"a"},
    guard = kInvalidPlanIndex,
    domain = Sequential,
    isNonBlocking = true
}
```

slices 按访问顺序排列，先 element select 后 member select。

---

### 2.6 WriteBackPlan：写回合并结果

WriteBackPlan 将多条 WriteIntent 按 (target + domain + event) 分组合并，生成 updateCond + nextValue 形式。定义在 `include/convert.hpp` 第 469-482 行：

```cpp
struct WriteBackPlan {
    struct Entry {
        PlanSymbolId target;
        SignalId signal = kInvalidPlanIndex;
        ControlDomain domain = ControlDomain::Unknown;
        ExprNodeId updateCond = kInvalidPlanIndex;
        ExprNodeId nextValue = kInvalidPlanIndex;
        std::vector<EventEdge> eventEdges;
        std::vector<ExprNodeId> eventOperands;
        slang::SourceLocation location{};
    };
    std::vector<Entry> entries;
};
```

**字段说明**：
- `entries`：写回合并条目数组（每个 Entry 对应一个 target + domain + event 组合）

**Entry 字段说明**：
- `target`：写回目标符号（PlanSymbolId）
- `signal`：信号索引（SignalId）
- `domain`：控制域（Combinational / Sequential / Latch / Unknown）
- `updateCond`：写回触发条件（guard OR 合并结果，ExprNodeId）
- `nextValue`：合并后的 nextValue（mux 链结果，ExprNodeId）
- `eventEdges`：顺序域事件边沿列表（EventEdge 数组）
- `eventOperands`：顺序域事件信号列表（ExprNodeId 数组）

**例子**：顺序写回合并（以 PlanEntry 为起点）

SystemVerilog 源码：

```systemverilog
always_ff @(posedge clk) begin
    if (rst) q <= 1'b0;
    else if (en) q <= d;
end
```

Pass6 执行后：

```cpp
PlanEntry entry = {
    status = Done,
    plan = { /* ... */ },
    artifacts = {
        loweringPlan = {
            /* Pass5 生成的 values 和 writes */
            values = {
                [0] = {kind=Constant, literal="1'b0"},
                [1] = {kind=Symbol, symbol="d"},
                [2] = {kind=Symbol, symbol="rst"},
                [3] = {kind=Symbol, symbol="en"},
                [4] = {kind=Symbol, symbol="clk"},
                [5] = {kind=Operation, op=kLogicNot, operands={2}},
                [6] = {kind=Operation, op=kLogicAnd, operands={5,3}},
                [7] = {kind=Operation, op=kLogicOr, operands={2,6}},
                [8] = {kind=Operation, op=kMux, operands={6,1,0}}
            },
            writes = {
                [0] = {target="q", value=0, guard=2, domain=Sequential},
                [1] = {target="q", value=1, guard=6, domain=Sequential}
            }
        },
        writeBackPlan = {
            entries = {
                [0] = {
                    target = "q",
                    signal = 0,
                    domain = Sequential,
                    updateCond = 7,
                    nextValue = 8,
                    eventEdges = {Posedge},
                    eventOperands = {4}
                }
            }
        }
    }
}
```

合并规则：
- updateCond = rst || ((!rst) && en) → values[7]
- nextValue = ((!rst) && en) ? d : 0 → values[8]
- 先写的 reset 优先级更高，mux 链保持语句顺序

---

## 3. 算法流程（动态处理）

算法遍历、转换、合并数据结构，完成从 AST 到 GRH 的转换。

### 3.1 ConvertDriver：入口与主流程

ConvertDriver 是 Convert 流程的入口类，封装了整个转换过程。定义在 `include/convert.hpp` 第 657-672 行：

```cpp
class ConvertDriver {
public:
    explicit ConvertDriver(ConvertOptions options = {});
    grh::ir::Netlist convert(const slang::ast::RootSymbol& root);
    ConvertDiagnostics& diagnostics() noexcept;
    ConvertLogger& logger() noexcept;

private:
    ConvertOptions options_;
    ConvertDiagnostics diagnostics_;
    ConvertLogger logger_;
    PlanCache planCache_;
    PlanTaskQueue planQueue_;
};
```

**函数入口**：

```cpp
// 输入
const slang::ast::RootSymbol& root  // slang 解析后的 AST 根节点

// 输出
grh::ir::Netlist  // GRH 网表

// 函数签名
grh::ir::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root);
```

**内部处理流程**（convert 函数内）：

```cpp
grh::ir::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root) {
    // 1. 清空缓存和队列
    planCache_.clear();
    planQueue_.reset();
    
    // 2. 组装 ConvertContext
    ConvertContext ctx;
    ctx.compilation = &root.getCompilation();
    ctx.root = &root;
    ctx.options = options_;
    ctx.diagnostics = &diagnostics_;
    ctx.logger = &logger_;
    ctx.planCache = &planCache_;
    ctx.planQueue = &planQueue_;
    
    // 3. 创建空 Netlist
    grh::ir::Netlist netlist;
    
    // 4. 投递顶层实例任务
    for (const auto* top : root.topInstances) {
        PlanKey key;
        key.definition = top->getDefinition();
        key.body = &top->body;
        key.paramSignature = snapshotParameters(top->body, nullptr).signature;
        planQueue_.push(key);
    }
    
    // 5. 主循环：处理队列中的模块
    PlanKey key;
    while (planQueue_.tryPop(key)) {
        // 获取或创建 PlanEntry
        planCache_.tryClaim(key);
        
        // Pass1: SymbolCollector
        ModulePlanner planner(ctx);
        ModulePlan plan = planner.plan(*key.body);
        planCache_.storePlan(key, std::move(plan));
        
        // Pass2: TypeResolver
        TypeResolverPass pass2(ctx);
        pass2.resolve(/* plan from cache */);
        
        // Pass3: RWAnalyzer
        RWAnalyzerPass pass3(ctx);
        pass3.analyze(/* plan from cache */);
        
        // Pass4: ExprLowerer
        ExprLowererPass pass4(ctx);
        LoweringPlan lowering = pass4.lower(/* plan */);
        planCache_.setLoweringPlan(key, std::move(lowering));
        
        // Pass5: StmtLowerer
        StmtLowererPass pass5(ctx);
        pass5.lower(/* plan, lowering */);
        
        // Pass6: WriteBack
        WriteBackPass pass6(ctx);
        WriteBackPlan writeBack = pass6.lower(/* plan, lowering */);
        planCache_.setWriteBackPlan(key, std::move(writeBack));
        
        // Pass7: MemoryPortLowerer
        MemoryPortLowererPass pass7(ctx);
        pass7.lower(/* plan, lowering */);
    }
    
    // 6. Pass8: GraphAssembly（遍历所有已完成的 PlanEntry）
    GraphAssembler assembler(ctx, netlist);
    for (each PlanEntry in planCache_) {
        assembler.build(key, plan, lowering, writeBack);
    }
    
    return netlist;
}
```

**Pass 流水线**：

```
AST (slang) 
    → Pass1 (SymbolCollector) → ModulePlan (骨架) → 存入 PlanEntry.plan
    → Pass2 (TypeResolver)    → ModulePlan (类型填充)
    → Pass3 (RWAnalyzer)      → ModulePlan (rwOps/memPorts)
    → Pass4 (ExprLowerer)     → LoweringPlan (values/roots) → 存入 PlanEntry.artifacts.loweringPlan
    → Pass5 (StmtLowerer)     → LoweringPlan (+writes)
    → Pass6 (WriteBack)       → WriteBackPlan (entries) → 存入 PlanEntry.artifacts.writeBackPlan
    → Pass7 (MemoryPort)      → LoweringPlan (+memoryReads/Writes)
    → Pass8 (GraphAssembly)   → Netlist/Graph (GRH)
```

---

### 3.2 Pass1：符号收集

**输入**：InstanceBodySymbol（slang AST）

**输出**：PlanEntry.plan（填充骨架的 ModulePlan）

**算法步骤**：

1. 初始化 ModulePlan，绑定 body 指针
2. 遍历 body.getPortList()，为每个 PortSymbol 创建 PortInfo
3. 遍历 body.members()，为 NetSymbol/VariableSymbol 创建 SignalInfo
4. 遍历实例符号（InstanceSymbol/InstanceArraySymbol），创建 InstanceInfo
5. 递归处理 GenerateBlock，将发现的子模块投递到 PlanTaskQueue

**例子**：端口收集的数据结构更新

遍历前（PlanEntry 初始状态）：

```cpp
PlanEntry entry = {
    status = Planning,
    plan = {
        body = &test_body,
        symbolTable = {},
        ports = {},
        signals = {},
        rwOps = {},
        memPorts = {},
        instances = {}
    }
}
```

遍历 body.getPortList() 发现端口 a：

```cpp
entry.plan.symbolTable.intern("a")  // 返回 id(0)
entry.plan.symbolTable.intern("y")  // 返回 id(1)

entry.plan.ports[0] = {
    symbol = id(0),
    direction = Input,
    width = 0,
    isSigned = false
}

entry.plan.ports[1] = {
    symbol = id(1),
    direction = Output,
    width = 0,
    isSigned = false
}
```

---

### 3.3 Pass2：类型解析

**输入**：PlanEntry.plan（Pass1 输出的 ModulePlan）

**输出**：原地更新 PlanEntry.plan（填充 width/isSigned/memoryRows 等）

**算法步骤**：

1. 根据 symbolTable 建立端口/信号名到索引的映射
2. 回扫 PortSymbol，获取 Type，计算位宽和符号性
3. 回扫 NetSymbol/VariableSymbol：
   - 剥离 type alias
   - 收集 unpacked 维度，累乘计算 memoryRows
   - 收集 packed 维度到 packedDims
   - 计算元素类型宽度

**例子**：位宽计算的数据结构更新

输入 PlanEntry：

```cpp
entry.plan.signals = {
    [0] = {
        symbol = "mem",
        kind = Variable,
        width = 0,
        memoryRows = 0,
        packedDims = {},
        unpackedDims = {}
    }
}
```

发现类型为 `logic [7:0] mem [0:15]`：

```cpp
// 收集 unpacked 维度
entry.plan.signals[0].unpackedDims[0] = {
    extent = 16,
    left = 0,
    right = 15
}
entry.plan.signals[0].memoryRows = 16

// 收集 packed 维度并计算宽度
entry.plan.signals[0].packedDims[0] = 8
entry.plan.signals[0].width = 8
entry.plan.signals[0].isSigned = false
```

---

### 3.4 Pass3：读写分析

**输入**：PlanEntry.plan（ModulePlan + AST）

**输出**：原地更新 PlanEntry.plan.rwOps 和 PlanEntry.plan.memPorts

**算法步骤**：

1. 建立 PlanSymbolId -> SignalId 映射
2. 初始化 RWAnalyzerState（访问计数器）
3. 遍历 ProceduralBlockSymbol，根据 procedureKind 分类 ControlDomain
4. 遍历 ContinuousAssignSymbol，固定为 Combinational 域
5. AST 访问者模式遍历语句：
   - LHS 递归标记为写
   - RHS/条件/索引标记为读
   - 每次访问追加 AccessSite{location, sequence}
6. Memory 访问识别（memoryRows > 0），生成 MemoryPortInfo

**例子**：读写关系的数据结构更新

SystemVerilog 源码：

```systemverilog
always_ff @(posedge clk) begin
    if (en) q <= d;
end
```

访问者遍历过程：

```cpp
// 访问 NamedValueExpression "d" 在 RHS
recordRead(target="d", domain=Sequential, location=@line2)

// 访问 NamedValueExpression "en" 在条件
recordRead(target="en", domain=Sequential, location=@line1)

// 访问 NamedValueExpression "q" 在 LHS
recordWrite(target="q", domain=Sequential, location=@line2)
```

PlanEntry 更新后：

```cpp
entry.plan.rwOps = {
    [0] = {target="d", domain=Sequential, isWrite=false, sites={[@line2, seq=0]}},
    [1] = {target="en", domain=Sequential, isWrite=false, sites={[@line1, seq=1]}},
    [2] = {target="q", domain=Sequential, isWrite=true, sites={[@line2, seq=2]}}
}
```

---

### 3.5 Pass4：表达式降级

**输入**：PlanEntry.plan（ModulePlan）+ 表达式 AST

**输出**：PlanEntry.artifacts.loweringPlan（values/roots/tempSymbols）

**算法**：递归下降 + 节点缓存

伪代码：

```cpp
ExprNodeId lowerExpression(const Expression& expr) {
    // 缓存命中检查
    if (lowered.contains(&expr)) return lowered[&expr];
    
    ExprNode node;
    
    // 根据表达式类型分发
    if (auto* named = expr.as_if<NamedValueExpression>()) {
        node.kind = Symbol;
        node.symbol = lookup(named->symbol.name);
    } else if (auto* binary = expr.as_if<BinaryExpression>()) {
        ExprNodeId lhs = lowerExpression(binary->left());
        ExprNodeId rhs = lowerExpression(binary->right());
        node.kind = Operation;
        node.op = mapBinaryOp(binary->op);
        node.operands = {lhs, rhs};
        node.tempSymbol = makeTempSymbol();
    }
    // ... 其他类型
    
    // 缓存并返回
    ExprNodeId id = addNode(expr, std::move(node));
    lowered[&expr] = id;
    return id;
}
```

**例子**：二元表达式降级过程

SystemVerilog 源码：

```systemverilog
assign y = a + b;
```

调用栈与 PlanEntry 更新：

```cpp
lowerExpression("a + b")
    → node.op = kAdd
    → lowerExpression("a")
        → node.kind = Symbol, node.symbol = "a"
        → entry.artifacts.loweringPlan.values[0] = node
        → return 0
    → lowerExpression("b")
        → node.kind = Symbol, node.symbol = "b"
        → entry.artifacts.loweringPlan.values[1] = node
        → return 1
    → node.operands = {0, 1}
    → entry.artifacts.loweringPlan.values[2] = node
    → entry.artifacts.loweringPlan.roots[0] = {value=2}
    → return 2
```

最终 PlanEntry：

```cpp
entry.artifacts.loweringPlan = {
    values = {
        [0] = {kind=Symbol, symbol="a"},
        [1] = {kind=Symbol, symbol="b"},
        [2] = {kind=Operation, op=kAdd, operands={0,1}, tempSymbol="_expr_tmp_0"}
    },
    roots = {[0]={value=2}},
    tempSymbols = {[0]="_expr_tmp_0"},
    writes = {},
    loweredStmts = {},
    dpiImports = {},
    memoryReads = {},
    memoryWrites = {}
}
```

---

### 3.6 Pass5：语句降级

**输入**：PlanEntry.plan（ModulePlan）+ PlanEntry.artifacts.loweringPlan

**输出**：更新的 PlanEntry.artifacts.loweringPlan（追加 guard 节点、WriteIntent、LoweredStmt）

**核心机制**：Guard 栈

- `guardStack` 维护当前控制路径的 guard 链
- 进入 if-true 分支：push(guard && cond)
- 进入 if-false 分支：push(guard && !cond)
- 退出分支：pop()

**例子**：if/else 的 guard 传播与 PlanEntry 更新

SystemVerilog 源码：

```systemverilog
if (a) begin
    y = b;
end else begin
    y = c;
end
```

算法执行与数据结构更新：

```cpp
// 初始状态
guardStack = {}  // 空，无条件

// 处理 if (a)
baseGuard = currentGuard()  // kInvalid（无条件）
condA = lowerExpression("a")  // entry.artifacts.loweringPlan.values[0] = Symbol("a")
trueGuard = combineGuard(baseGuard, condA)  // "a"
pushGuard(trueGuard)  // guardStack = {"a"}

// 处理 y = b
valueB = lowerExpression("b")  // entry.artifacts.loweringPlan.values[1] = Symbol("b")
guardB = currentGuard()  // "a"
entry.artifacts.loweringPlan.writes[0] = {
    target = "y",
    value = 1,
    guard = 0,  // 指向 values[0]
    domain = Combinational
}

popGuard()  // guardStack = {}

// 处理 else
notA = makeLogicNot(condA)  // entry.artifacts.loweringPlan.values[2] = Operation(kLogicNot, operands={0})
falseGuard = combineGuard(baseGuard, notA)  // "!a"
pushGuard(falseGuard)  // guardStack = {"!a"}

// 处理 y = c
valueC = lowerExpression("c")  // entry.artifacts.loweringPlan.values[3] = Symbol("c")
guardC = currentGuard()  // "!a"
entry.artifacts.loweringPlan.writes[1] = {
    target = "y",
    value = 3,
    guard = 2,  // 指向 values[2]
    domain = Combinational
}

popGuard()  // guardStack = {}
```

最终 PlanEntry：

```cpp
entry.artifacts.loweringPlan = {
    values = {
        [0] = {kind=Symbol, symbol="a"},
        [1] = {kind=Symbol, symbol="b"},
        [2] = {kind=Operation, op=kLogicNot, operands={0}},
        [3] = {kind=Symbol, symbol="c"}
    },
    writes = {
        [0] = {target="y", value=1, guard=0, domain=Combinational},
        [1] = {target="y", value=3, guard=2, domain=Combinational}
    }
}
```

---

### 3.7 Pass6：写回合并

**输入**：PlanEntry.plan（ModulePlan）+ PlanEntry.artifacts.loweringPlan

**输出**：PlanEntry.artifacts.writeBackPlan（entries）

**合并规则**：

1. **分组**：按 (target + domain + eventEdges + eventOperands) 分组
2. **Guard 合并**：updateCond = OR(guard_i)
3. **NextValue 合并**：按语句顺序构建 mux 链
   - next = mux(guard_n, value_n, ... , mux(guard_1, value_1, base))

**例子**：多条件写回合并与 PlanEntry 更新

SystemVerilog 源码：

```systemverilog
always_ff @(posedge clk) begin
    if (rst) q <= 0;
    else if (en) q <= d;
    else if (load) q <= data;
end
```

输入 PlanEntry（来自 Pass5）：

```cpp
entry.artifacts.loweringPlan = {
    values = {
        [0] = {kind=Constant, literal="0"},
        [1] = {kind=Symbol, symbol="d"},
        [2] = {kind=Symbol, symbol="data"},
        [3] = {kind=Symbol, symbol="rst"},
        [4] = {kind=Symbol, symbol="en"},
        [5] = {kind=Symbol, symbol="load"},
        [6] = {kind=Symbol, symbol="clk"},
        [7] = {kind=Operation, op=kLogicNot, operands={3}},     // !rst
        [8] = {kind=Operation, op=kLogicAnd, operands={7,4}},   // (!rst)&&en
        [9] = {kind=Operation, op=kLogicNot, operands={4}},     // !en
        [10] = {kind=Operation, op=kLogicAnd, operands={7,9}}, // (!rst)&&(!en)
        [11] = {kind=Operation, op=kLogicAnd, operands={10,5}}, // (!rst)&&(!en)&&load
        [12] = {kind=Operation, op=kLogicOr, operands={3,8}},   // rst||((!rst)&&en)
        [13] = {kind=Operation, op=kLogicOr, operands={12,11}}, // 全部guard的OR
        [14] = {kind=Operation, op=kMux, operands={8,1,0}},     // en ? d : 0
        [15] = {kind=Operation, op=kMux, operands={11,2,14}}    // load ? data : (en?d:0)
    },
    writes = {
        [0] = {target="q", value=0, guard=3, domain=Sequential},
        [1] = {target="q", value=1, guard=8, domain=Sequential},
        [2] = {target="q", value=2, guard=11, domain=Sequential}
    }
}
```

合并过程与 PlanEntry 更新：

```cpp
// 分组：三条写入同一组（target=q, domain=Sequential, event=posedge clk）

// Guard 合并
updateCond = entry.artifacts.loweringPlan.values[13]  // OR(guard_0, guard_1, guard_2)

// NextValue 合并（按语句顺序构建mux链）
nextValue = entry.artifacts.loweringPlan.values[15]

// 写入 PlanEntry
entry.artifacts.writeBackPlan = {
    entries = {
        [0] = {
            target = "q",
            signal = 0,
            domain = Sequential,
            updateCond = 13,
            nextValue = 15,
            eventEdges = {Posedge},
            eventOperands = {6}  // clk
        }
    }
}
```

---

### 3.8 Pass7：内存端口处理

**输入**：PlanEntry.plan.memPorts + PlanEntry.artifacts.loweringPlan

**输出**：更新 PlanEntry.artifacts.loweringPlan.memoryReads 和 memoryWrites

**算法步骤**：

1. 从 loweredStmts 中识别 mem[addr] 读访问
2. 从 writes 中识别 mem[addr] 写访问
3. 对读访问生成 MemoryReadPort
4. 对写访问生成 MemoryWritePort（支持 mask）
5. 多维 memory 地址线性化

**例子**：内存读写识别与 PlanEntry 更新

SystemVerilog 源码：

```systemverilog
logic [7:0] mem [0:15];
assign rdata = mem[addr];              // 读
always_ff @(posedge clk)
    if (en) mem[addr] <= wdata;        // 写
```

PlanEntry 更新（假设已有 PlanEntry.plan.memPorts）：

```cpp
// 读端口识别
entry.artifacts.loweringPlan.memoryReads[0] = {
    memory = "mem",
    signal = 0,
    address = ExprNodeId{"addr"},  // 指向 values 中 addr 节点
    data = ExprNodeId{...},         // mem[addr] 对应的 slice 节点
    isSync = false,
    updateCond = kInvalidPlanIndex
}

// 写端口识别
entry.artifacts.loweringPlan.memoryWrites[0] = {
    memory = "mem",
    signal = 0,
    address = ExprNodeId{"addr"},
    data = ExprNodeId{"wdata"},
    mask = kInvalidPlanIndex,  // 全写，无掩码
    updateCond = ExprNodeId{"en"},
    isMasked = false,
    eventEdges = {Posedge},
    eventOperands = {ExprNodeId{"clk"}}
}
```

---

### 3.9 Pass8：图组装

**输入**：PlanEntry（包含 plan、loweringPlan、writeBackPlan）+ Netlist

**输出**：填充后的 Netlist（新增 Graph）

**组装规则**：

1. **端口**：创建 input/output/inout Value，绑定到 Graph ports
2. **表达式**：递归发射 ExprNode 为 GRH Operation
3. **写回**：
   - comb → kAssign(nextValue)
   - seq → kRegister(updateCond, nextValue, eventOperands...)
   - latch → kLatch(updateCond, nextValue)
4. **Memory**：创建 kMemory，发射 kMemoryReadPort/kMemoryWritePort
5. **实例化**：生成 kInstance/kBlackbox

**例子**：寄存器写回发射

输入 PlanEntry：

```cpp
entry.artifacts.writeBackPlan.entries[0] = {
    target = "q",
    signal = 0,
    domain = Sequential,
    updateCond = ExprNodeId{...},  // en
    nextValue = ExprNodeId{...},   // d
    eventEdges = {Posedge},
    eventOperands = {ExprNodeId{"clk"}}
}
```

发射的 GRH Operation：

```cpp
Operation op = {
    kind = kRegister,
    operands = {ValueId{updateCond}, ValueId{nextValue}, ValueId{"clk"}},
    results = {ValueId{"q"}},
    attrs = {
        eventEdge = "posedge"
    }
}
```

---

## 4. 完整示例

### 4.1 示例源码

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

### 4.2 各阶段数据结构状态

#### Pass1 后：PlanEntry

```cpp
PlanEntry entry = {
    status = Done,
    plan = {
        body = &counter_body,
        symbolTable = {
            "clk" -> id(0),
            "rst" -> id(1),
            "en" -> id(2),
            "count" -> id(3)
        },
        moduleSymbol = id(3),  // "counter"
        ports = {
            [0] = {symbol=id(0), direction=Input, width=0, isSigned=false},
            [1] = {symbol=id(1), direction=Input, width=0, isSigned=false},
            [2] = {symbol=id(2), direction=Input, width=0, isSigned=false},
            [3] = {symbol=id(3), direction=Output, width=0, isSigned=false}
        },
        signals = {},
        rwOps = {},
        memPorts = {},
        instances = {}
    },
    artifacts = {}
}
```

#### Pass2 后：PlanEntry.plan 更新

```cpp
entry.plan.ports[0].width = 1
entry.plan.ports[1].width = 1
entry.plan.ports[2].width = 1
entry.plan.ports[3].width = 4
```

#### Pass3 后：PlanEntry.plan.rwOps 更新

```cpp
entry.plan.rwOps = {
    [0] = {target=1, domain=Sequential, isWrite=false, sites={[loc=@line7, seq=0]}},   // rst
    [1] = {target=2, domain=Sequential, isWrite=false, sites={[loc=@line9, seq=1]}},   // en
    [2] = {target=3, domain=Sequential, isWrite=false, sites={[loc=@line9, seq=2]}},   // count (RHS)
    [3] = {target=3, domain=Sequential, isWrite=true, sites={[loc=@line8, seq=3], [loc=@line9, seq=4]}}  // count (LHS)
}
```

#### Pass4-5 后：PlanEntry.artifacts.loweringPlan

```cpp
entry.artifacts.loweringPlan = {
    values = {
        [0] = {kind=Symbol, symbol="rst"},
        [1] = {kind=Constant, literal="4'b0"},
        [2] = {kind=Symbol, symbol="en"},
        [3] = {kind=Symbol, symbol="count"},
        [4] = {kind=Constant, literal="4'b1"},
        [5] = {kind=Operation, op=kAdd, operands={3,4}, tempSymbol="_expr_tmp_0"},
        [6] = {kind=Symbol, symbol="clk"},
        [7] = {kind=Operation, op=kLogicNot, operands={0}},
        [8] = {kind=Operation, op=kLogicAnd, operands={7,2}}
    },
    roots = {
        [0] = {value=1},
        [1] = {value=5}
    },
    writes = {
        [0] = {target="count", value=1, guard=0, domain=Sequential, isNonBlocking=true},
        [1] = {target="count", value=5, guard=8, domain=Sequential, isNonBlocking=true}
    },
    loweredStmts = {
        [0] = {kind=Write, write=writes[0], eventEdges={Posedge}, eventOperands={6}},
        [1] = {kind=Write, write=writes[1], eventEdges={Posedge}, eventOperands={6}}
    }
}
```

#### Pass6-7 后：PlanEntry.artifacts 完整

```cpp
entry.artifacts = {
    loweringPlan = {
        values = {
            [0] = {kind=Symbol, symbol="rst"},
            [1] = {kind=Constant, literal="4'b0"},
            [2] = {kind=Symbol, symbol="en"},
            [3] = {kind=Symbol, symbol="count"},
            [4] = {kind=Constant, literal="4'b1"},
            [5] = {kind=Operation, op=kAdd, operands={3,4}},
            [6] = {kind=Symbol, symbol="clk"},
            [7] = {kind=Operation, op=kLogicNot, operands={0}},
            [8] = {kind=Operation, op=kLogicAnd, operands={7,2}},
            [9] = {kind=Operation, op=kLogicOr, operands={0,8}},
            [10] = {kind=Operation, op=kMux, operands={8,5,1}}
        },
        roots = {...},
        writes = {...},
        memoryReads = {},
        memoryWrites = {}
    },
    writeBackPlan = {
        entries = {
            [0] = {
                target = "count",
                signal = 3,
                domain = Sequential,
                updateCond = 9,
                nextValue = 10,
                eventEdges = {Posedge},
                eventOperands = {6}
            }
        }
    }
}
```

#### Pass8 后：GRH Graph

```cpp
Graph "counter" {
    // 端口
    Input: clk(1), rst(1), en(1)
    Output: count(4)
    
    // 内部 Value 和 Operation
    v0: kConstant("4'b0")
    v1: kConstant("4'b1")
    v2: kAdd(count, v1)  // count + 1
    v3: kMux(en, v2, v0) // en ? (count+1) : 0
    
    // 寄存器写回
    op0: kRegister(
        operands = {en, v3, clk},
        results = {count},
        attrs = {eventEdge = "posedge"}
    )
}
```

---

## 5. 代码映射

### 5.1 文件位置

- 数据结构定义：`include/convert.hpp`
- Pass 实现：`src/convert.cpp`
- 测试用例：`tests/data/convert/*.sv`
- 测试代码：`tests/convert/test_convert_*.cpp`

### 5.2 关键定义位置

**数据结构**（`include/convert.hpp`）：

| 名称 | 类型 | 行号 | 说明 |
|------|------|------|------|
| ConvertContext | struct | 166 | 共享状态容器 |
| PlanCache | class | 525 | 模块计划缓存 |
| PlanKey | struct | 489 | 模块标识键 |
| PlanKeyHash | struct | 502 | PlanKey 哈希函数 |
| PlanEntry | struct | 519 | 计划条目 |
| ModulePlan | struct | 268 | 模块骨架 |
| LoweringPlan | struct | 458 | 降级计划 |
| WriteIntent | struct | 365 | 写回意图 |
| WriteSlice | struct | 355 | 切片描述 |
| WriteBackPlan | struct | 469 | 写回合并结果 |

**Pass 类**（`include/convert.hpp`）：

| 名称 | 行号 | 说明 |
|------|------|------|
| ConvertDriver | 657 | 入口类 |
| ModulePlanner | 569 | Pass1：符号收集 |
| TypeResolverPass | 579 | Pass2：类型解析 |
| RWAnalyzerPass | 589 | Pass3：读写分析 |
| ExprLowererPass | 599 | Pass4：表达式降级 |
| StmtLowererPass | 609 | Pass5：语句降级 |
| WriteBackPass | 619 | Pass6：写回合并 |
| MemoryPortLowererPass | 629 | Pass7：内存端口处理 |
| GraphAssembler | 639 | Pass8：图组装 |

### 5.3 函数入口

**ConvertDriver**（`include/convert.hpp` 第 657-672 行）：

```cpp
// 构造函数
explicit ConvertDriver(ConvertOptions options = {});

// 主入口函数
grh::ir::Netlist convert(const slang::ast::RootSymbol& root);

// 访问器
ConvertDiagnostics& diagnostics() noexcept;
ConvertLogger& logger() noexcept;
```

**PlanCache 接口**：

```cpp
bool tryClaim(const PlanKey& key);
void storePlan(const PlanKey& key, ModulePlan plan);
void markFailed(const PlanKey& key);
std::optional<ModulePlan> findReady(const PlanKey& key) const;
bool setLoweringPlan(const PlanKey& key, LoweringPlan plan);
bool setWriteBackPlan(const PlanKey& key, WriteBackPlan plan);
std::optional<LoweringPlan> getLoweringPlan(const PlanKey& key) const;
std::optional<WriteBackPlan> getWriteBackPlan(const PlanKey& key) const;
```

**各 Pass 入口**（实现位于 `src/convert.cpp`）：

```cpp
// Pass1
ModulePlan ModulePlanner::plan(const slang::ast::InstanceBodySymbol& body);

// Pass2
void TypeResolverPass::resolve(ModulePlan& plan);

// Pass3
void RWAnalyzerPass::analyze(ModulePlan& plan);

// Pass4
LoweringPlan ExprLowererPass::lower(ModulePlan& plan);

// Pass5
void StmtLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering);

// Pass6
WriteBackPlan WriteBackPass::lower(ModulePlan& plan, LoweringPlan& lowering);

// Pass7
void MemoryPortLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering);

// Pass8
grh::ir::Graph& GraphAssembler::build(
    const PlanKey& key,
    const ModulePlan& plan,
    LoweringPlan& lowering,
    const WriteBackPlan& writeBack
);
```

---

## 附录：术语表

- **GRH**：Graph-based Hardware IR，基于图的硬件中间表示
- **SSA**：Static Single Assignment，静态单赋值形式
- **Guard**：控制流条件的逻辑累积，决定写回何时生效
- **Lowering**：从高阶表示降级到低阶表示的过程
- **Plan**：转换过程中的中间数据结构
- **Pass**：对数据结构的一次遍历处理
- **slang**：SystemVerilog 前端解析器
- **InstanceBodySymbol**：slang 中表示模块实例体的符号
