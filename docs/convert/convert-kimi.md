# Convert 流程技术文档

本文档按"先数据结构、后算法"的思路组织，直接对应 `include/convert.hpp` 与 `src/convert.cpp` 的当前实现。

---

## 目录

1. 数据结构描述规范
2. 数据结构（静态骨架）
   - 2.1 ConvertContext：共享状态
   - 2.2 PlanCache / PlanKey / PlanEntry / PlanTaskQueue
   - 2.3 ModulePlan：模块骨架
   - 2.4 LoweringPlan：降级计划与表达式/语句
   - 2.5 WriteIntent / WriteSlice：写回意图
   - 2.6 WriteBackPlan：写回合并结果
3. 算法流程（动态处理）
   - 3.1 ConvertDriver：入口与主流程
   - 3.2 ModulePlanner：收集参数/端口/信号/实例
   - 3.3 StmtLowererPass：表达式与语句降级
   - 3.4 WriteBackPass：写回合并
   - 3.5 MemoryPortLowererPass：内存端口处理
   - 3.6 GraphAssembler：图组装
4. 完整示例
   - 4.1 示例源码
   - 4.2 各阶段数据结构状态（示意）
5. 代码映射

---

## 1. 数据结构描述规范

本文档采用简写形式描述数据结构：

**数组**：`{ [index] = value, ... }`

```cpp
values = {
    [0] = {kind=Symbol, symbol="a"},
    [1] = {kind=Operation, op=kAnd, operands={0,2}}
}
```

**结构体**：`{ field1=value1, field2=value2, ... }`

```cpp
plan = {
    body = &counter_body,
    ports = {
        [0] = {symbol="clk", direction=Input, width=1},
        [1] = {symbol="rst", direction=Input, width=1}
    }
}
```

**PlanEntry 作为根节点**：

```cpp
PlanEntry entry = {
    status = Done,
    plan = { body=..., ports=..., signals=..., instances=... },
    artifacts = {
        loweringPlan = { values=..., writes=..., loweredStmts=... },
        writeBackPlan = { entries=... }
    }
}
```

不相关字段使用 `...` 省略。

---

## 2. 数据结构（静态骨架）

### 2.1 ConvertContext：共享状态

贯穿转换流程的共享状态容器（`include/convert.hpp` 约 166 行）：

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

---

### 2.2 PlanCache / PlanKey / PlanEntry / PlanTaskQueue

**PlanKey**：模块计划的唯一键（`include/convert.hpp` 约 458 行）。当 `definition` 存在时，比较 `definition + paramSignature`；否则比较 `body + paramSignature`。

```cpp
struct PlanKey {
    const slang::ast::DefinitionSymbol* definition = nullptr;
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string paramSignature;

    bool operator==(const PlanKey& other) const noexcept {
        if (definition || other.definition) {
            return definition == other.definition && paramSignature == other.paramSignature;
        }
        return body == other.body && paramSignature == other.paramSignature;
    }
};
```

**PlanEntry / PlanArtifacts**：缓存条目（`include/convert.hpp` 约 453 行）：

```cpp
struct PlanArtifacts {
    std::optional<LoweringPlan> loweringPlan;
    std::optional<WriteBackPlan> writeBackPlan;
};

enum class PlanStatus { Pending, Planning, Done, Failed };

struct PlanEntry {
    PlanStatus status = PlanStatus::Pending;
    std::optional<ModulePlan> plan;
    PlanArtifacts artifacts;
};
```

**PlanCache**：全局缓存（`include/convert.hpp` 约 494 行）。核心能力：
- `tryClaim` 用于抢占/去重
- `storePlan` 保存已完成的 `ModulePlan`
- `setLoweringPlan / setWriteBackPlan` 写入中间产物

```cpp
class PlanCache {
    std::unordered_map<PlanKey, PlanEntry, PlanKeyHash> entries_;
};
```

**PlanTaskQueue**：模块任务队列（`include/convert.hpp` 约 523 行）：

```cpp
class PlanTaskQueue {
public:
    void push(PlanKey key);
    bool tryPop(PlanKey& out);
    void close();
    void reset();
private:
    std::deque<PlanKey> queue_;
};
```

**例子**：PlanEntry 的状态变化

```cpp
// 初始状态
entry = { status = Pending, plan = nullopt, artifacts = {} }

// tryClaim 后（处理中）
entry = { status = Planning, plan = nullopt }

// ModulePlanner + Pass 完成后
entry = {
    status = Done,
    plan = { body=..., ports=..., signals=..., instances=... },
    artifacts = { loweringPlan=..., writeBackPlan=... }
}
```

---

### 2.3 ModulePlan：模块骨架

定义在 `include/convert.hpp` 约 244 行：

```cpp
struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    PlanSymbolTable symbolTable;
    PlanSymbolId moduleSymbol;
    std::vector<PortInfo> ports;
    std::vector<SignalInfo> signals;
    std::vector<InstanceInfo> instances;
};
```

相关结构（节选）：

```cpp
struct PortInfo {
    PlanSymbolId symbol;
    PortDirection direction = PortDirection::Input;
    int32_t width = 0;
    bool isSigned = false;
    std::optional<InoutBinding> inoutSymbol; // base__in/base__out/base__oe
};

struct SignalInfo {
    PlanSymbolId symbol;
    SignalKind kind = SignalKind::Net;
    int32_t width = 0;
    bool isSigned = false;
    int64_t memoryRows = 0;
    std::vector<int32_t> packedDims;
    std::vector<UnpackedDimInfo> unpackedDims;
};

struct InstanceInfo {
    const slang::ast::InstanceSymbol* instance = nullptr;
    PlanSymbolId instanceSymbol;
    PlanSymbolId moduleSymbol;
    bool isBlackbox = false;
    std::vector<InstanceParameter> parameters;
    std::string paramSignature;
};
```

---

### 2.4 LoweringPlan：降级计划与表达式/语句

**ExprNode**：表达式 DAG 节点（`include/convert.hpp` 约 300 行）：

```cpp
struct ExprNode {
    ExprNodeKind kind = ExprNodeKind::Invalid;
    grh::ir::OperationKind op = grh::ir::OperationKind::kConstant;
    PlanSymbolId symbol;
    PlanSymbolId tempSymbol;
    std::string literal;
    std::vector<ExprNodeId> operands;
    int32_t widthHint = 0;
    slang::SourceLocation location{};
};
```

**LoweredStmt**：语句级降级结果，包含写回、display/assert/dpi 调用（`include/convert.hpp` 约 390 行）：

```cpp
struct LoweredStmt {
    LoweredStmtKind kind = LoweredStmtKind::Write;
    grh::ir::OperationKind op = grh::ir::OperationKind::kAssign;
    ExprNodeId updateCond = kInvalidPlanIndex;
    std::vector<EventEdge> eventEdges;
    std::vector<ExprNodeId> eventOperands;
    slang::SourceLocation location{};
    WriteIntent write;
    DisplayStmt display;
    AssertStmt assertion;
    DpiCallStmt dpiCall;
};
```

**MemoryReadPort / MemoryWritePort**：内存端口信息（`include/convert.hpp` 约 403 行）。

**LoweringPlan** 定义（`include/convert.hpp` 约 428 行）：

```cpp
struct LoweringPlan {
    std::vector<ExprNode> values;
    std::vector<PlanSymbolId> tempSymbols;
    std::vector<WriteIntent> writes;
    std::vector<LoweredStmt> loweredStmts;
    std::vector<DpiImportInfo> dpiImports;
    std::vector<MemoryReadPort> memoryReads;
    std::vector<MemoryWritePort> memoryWrites;
};
```

要点：
- `writes` 保存原始写回意图，`loweredStmts` 承载语句级信息（含时序/事件绑定）。
- `memoryReads/memoryWrites` 在 `MemoryPortLowererPass` 中生成。
- 不存在 `roots`/`LoweredRoot` 字段。

---

### 2.5 WriteIntent / WriteSlice：写回意图

**WriteIntent**（`include/convert.hpp` 约 335 行）：

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

**WriteSlice**（`include/convert.hpp` 约 325 行）：

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

**kind 枚举**：
- `None`：无切片
- `BitSelect`：位选择 `y[3]`
- `RangeSelect`：范围选择 `y[7:4]` 或 `y[base +: width]`
- `MemberSelect`：结构体/联合体成员选择

---

### 2.6 WriteBackPlan：写回合并结果

定义在 `include/convert.hpp` 约 438 行：

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

---

## 3. 算法流程（动态处理）

### 3.1 ConvertDriver：入口与主流程

定义在 `include/convert.hpp` 约 596 行。

**主入口**：`grh::ir::Netlist convert(const slang::ast::RootSymbol& root)`

**处理流程**（`src/convert.cpp` 约 11833 行）：

```cpp
grh::ir::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root) {
    grh::ir::Netlist netlist;
    planCache_.clear();
    planQueue_.reset();

    ConvertContext context{...};

    ModulePlanner planner(context);
    StmtLowererPass stmtLowerer(context);
    WriteBackPass writeBack(context);
    MemoryPortLowererPass memoryPortLowerer(context);
    GraphAssembler graphAssembler(context, netlist);

    std::unordered_set<PlanKey, PlanKeyHash> topKeys;
    std::unordered_map<PlanKey, std::vector<std::string>, PlanKeyHash> topAliases;
    for (const auto* topInstance : root.topInstances) {
        ParameterSnapshot params = snapshotParameters(topInstance->body, nullptr);
        PlanKey topKey = { &topInstance->body.getDefinition(), &topInstance->body,
                           params.signature };
        topKeys.insert(topKey);
        topAliases[topKey].push_back(topInstance->name);
        topAliases[topKey].push_back(topInstance->getDefinition().name);
        enqueuePlanKey(context, topInstance->body, std::move(params.signature));
    }

    PlanKey key;
    while (planQueue_.tryPop(key)) {
        if (!key.body) continue;
        if (!key.definition) key.definition = &key.body->getDefinition();
        if (!planCache_.tryClaim(key)) continue;

        ModulePlan plan = planner.plan(*key.body);
        LoweringPlan lowering;
        stmtLowerer.lower(plan, lowering);
        WriteBackPlan writeBackPlan = writeBack.lower(plan, lowering);
        memoryPortLowerer.lower(plan, lowering);
        ensureUniqueTempSymbols(plan, lowering); // 当前实现为空

        grh::ir::Graph& graph = graphAssembler.build(key, plan, lowering, writeBackPlan);

        if (topKeys.find(key) != topKeys.end()) {
            netlist.markAsTop(graph.symbol());
            // 按实例名/定义名注册别名，冲突会告警并跳过
            ...
        }

        planCache_.setLoweringPlan(key, std::move(lowering));
        planCache_.setWriteBackPlan(key, std::move(writeBackPlan));
        planCache_.storePlan(key, std::move(plan));
    }

    return netlist;
}
```

---

### 3.2 ModulePlanner：收集参数/端口/信号/实例

定义在 `include/convert.hpp` 约 538 行；实现入口在 `src/convert.cpp` 约 6808 行。

**职责**：构造 `ModulePlan`，填充 `symbolTable / ports / signals / instances`。

**核心流程**：
1. 选取模块名（body.name 或 definition.name），写入 `moduleSymbol`。
2. `collectParameters`：将参数名写入 `symbolTable`。
3. `collectPorts`：收集端口方向、位宽、符号名；inout 端口生成 `__in/__out/__oe` 绑定。
4. `collectSignals`：收集 net/variable，填充宽度、符号、内存维度。
5. `collectInstances`：生成 `InstanceInfo`，根据 `isBlackbox` 决定参数快照；同时 `enqueuePlanKey` 投递子模块。

---

### 3.3 StmtLowererPass：表达式与语句降级

定义在 `include/convert.hpp` 约 548 行；实现入口在 `src/convert.cpp` 约 6825 行。

**职责**：遍历 `plan.body` 的成员，构造 `LoweringPlan`：
- `values / tempSymbols`：表达式 DAG
- `writes`：写回意图
- `loweredStmts`：带时序/事件信息的语句级输出（Write/Display/Assert/DpiCall）
- `dpiImports`：DPI import 签名

**内部状态**（`src/convert.cpp` 约 780 行）包括：
- `guardStack`（条件守卫栈）
- `eventContext`（timing/event 绑定）
- loop/case 流程辅助结构

---

### 3.4 WriteBackPass：写回合并

定义在 `include/convert.hpp` 约 558 行；实现入口在 `src/convert.cpp` 约 7026 行。

**职责**：从 `loweredStmts` 中提取 `Write` 语句，合并为 `WriteBackPlan::Entry`。

**核心规则**：
1. 按 `(target + domain + eventEdges + eventOperands)` 分组。
2. 过滤非法写入（缺失 RHS、缺少时序等）；内存写入交给 `MemoryPortLowererPass`。
3. 合并 `guard` 得到 `updateCond`（逻辑 OR）。
4. 生成 `nextValue`：对多条写入构建 mux 链，必要时处理切片写回。
5. 在组合域若条件不完备且使用目标值，可能转为 `Latch` 域。

---

### 3.5 MemoryPortLowererPass：内存端口处理

定义在 `include/convert.hpp` 约 568 行；实现入口在 `src/convert.cpp` 约 8721 行。

**职责**：识别内存读写，生成 `memoryReads / memoryWrites`。

**要点**：
- **读端口**：识别 `ExprNode` 中以 `kSliceDynamic` 链接到内存符号的访问；结合时序上下文生成 `MemoryReadPort`。
- **写端口**：基于 `WriteIntent.slices` 提取地址切片，生成写地址与掩码；对多维 unpacked 数组构造线性地址。
- **时序要求**：同步读/写必须具备边沿事件，否则告警并跳过。

---

### 3.6 GraphAssembler：图组装

定义在 `include/convert.hpp` 约 578 行；实现入口在 `src/convert.cpp` 约 11804 行。

**职责**：将 `ModulePlan + LoweringPlan + WriteBackPlan` 组装为 `grh::ir::Graph`。

**主要步骤**：
1. `resolveGraphName`：模块名 + 参数签名哈希，避免冲突。
2. 创建端口值（input/output/inout）。
3. 创建信号值与内存 `kMemory` 操作。
4. 发射内存读写端口（`kMemoryReadPort / kMemoryWritePort`）。
5. 发射 display/assert/dpi（`kDisplay / kAssert / kDpicCall`）。
6. 发射实例（`kInstance / kBlackbox`），并处理实例输出切片写回。
7. 发射写回：组合域 `kAssign`，时序域 `kRegister`，锁存域 `kLatch`。

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

### 4.2 各阶段数据结构状态（示意）

#### ModulePlanner 后：ModulePlan

```cpp
ModulePlan plan = {
    body = &counter_body,
    moduleSymbol = "counter",
    ports = {
        [0] = {symbol="clk", direction=Input, width=1},
        [1] = {symbol="rst", direction=Input, width=1},
        [2] = {symbol="en",  direction=Input, width=1},
        [3] = {symbol="count", direction=Output, width=4}
    },
    signals = {
        [0] = {symbol="count", kind=Variable, width=4, isSigned=false}
    },
    instances = {}
}
```

#### StmtLowererPass 后：LoweringPlan（示意）

```cpp
lowering.values = {
    [0] = {kind=Symbol, symbol="rst"},
    [1] = {kind=Constant, literal="4'b0"},
    [2] = {kind=Symbol, symbol="en"},
    [3] = {kind=Symbol, symbol="count"},
    [4] = {kind=Constant, literal="4'b1"},
    [5] = {kind=Operation, op=kAdd, operands={3,4}},
    [6] = {kind=Symbol, symbol="clk"},
    [7] = {kind=Operation, op=kLogicNot, operands={0}},
    [8] = {kind=Operation, op=kLogicAnd, operands={7,2}}
};

lowering.writes = {
    [0] = {target="count", value=1, guard=0, domain=Sequential, isNonBlocking=true},
    [1] = {target="count", value=5, guard=8, domain=Sequential, isNonBlocking=true}
};

lowering.loweredStmts = {
    [0] = {kind=Write, write=lowering.writes[0], updateCond=0,
           eventEdges={Posedge}, eventOperands={6}},
    [1] = {kind=Write, write=lowering.writes[1], updateCond=8,
           eventEdges={Posedge}, eventOperands={6}}
};
```

#### WriteBackPass 后：WriteBackPlan（示意）

```cpp
writeBack.entries = {
    [0] = {
        target = "count",
        domain = Sequential,
        updateCond = "rst || (!rst && en)",
        nextValue = "(!rst && en) ? (count + 1) : 0",
        eventEdges = {Posedge},
        eventOperands = {Symbol("clk")}
    }
};
```

#### GraphAssembler 后：GRH Graph（示意）

```cpp
Graph "counter" {
    Input:  clk(1), rst(1), en(1)
    Output: count(4)

    op0: kRegister(
        operands = {updateCond, nextValue, clk},
        attrs = {eventEdge=["posedge"]}
    ) -> count
}
```

---

## 5. 代码映射

### 5.1 文件位置

- 数据结构定义：`include/convert.hpp`
- Pass 实现：`src/convert.cpp`
- 测试用例：`tests/data/convert/*.sv`
- 测试代码：`tests/convert/test_convert_*.cpp`

### 5.2 关键定义（`include/convert.hpp`）

| 名称 | 类型 | 行号 | 功能 |
|------|------|------|------|
| ConvertContext | struct | 166 | 共享状态 |
| PortInfo | struct | 201 | 端口描述 |
| SignalInfo | struct | 220 | 信号/内存描述 |
| InstanceInfo | struct | 235 | 实例信息 |
| ModulePlan | struct | 244 | 模块骨架 |
| ExprNode | struct | 300 | 表达式节点 |
| WriteSlice | struct | 325 | 切片描述 |
| WriteIntent | struct | 335 | 写回意图 |
| LoweredStmt | struct | 390 | 语句级降级 |
| MemoryReadPort | struct | 403 | 内存读端口 |
| MemoryWritePort | struct | 415 | 内存写端口 |
| LoweringPlan | struct | 428 | 降级计划 |
| WriteBackPlan | struct | 438 | 写回合并结果 |
| PlanKey | struct | 458 | 模块标识键 |
| PlanEntry | struct | 488 | 计划条目 |
| PlanCache | class | 494 | 模块计划缓存 |
| PlanTaskQueue | class | 523 | 计划队列 |
| ModulePlanner | class | 538 | 计划构建 |
| StmtLowererPass | class | 548 | 语句降级 |
| WriteBackPass | class | 558 | 写回合并 |
| MemoryPortLowererPass | class | 568 | 内存端口处理 |
| GraphAssembler | class | 578 | 图组装 |
| ConvertDriver | class | 596 | 入口类 |

### 5.3 函数入口（`src/convert.cpp`）

```cpp
ModulePlan ModulePlanner::plan(const slang::ast::InstanceBodySymbol& body);          // ~6808
void StmtLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering);              // ~6825
WriteBackPlan WriteBackPass::lower(ModulePlan& plan, LoweringPlan& lowering);       // ~7026
void MemoryPortLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering);        // ~8721
grh::ir::Graph& GraphAssembler::build(const PlanKey& key, const ModulePlan& plan,
                                      LoweringPlan& lowering, const WriteBackPlan& writeBack); // ~11804
grh::ir::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root);         // ~11833
```

---

## 附录：术语表

- **GRH**：Graph-based Hardware IR
- **Guard**：控制流条件的逻辑累积
- **Lowering**：从高阶表示降级到低阶表示
- **Plan**：转换过程中的中间数据结构
- **Pass**：对数据结构的一次遍历处理
- **slang**：SystemVerilog 前端解析器
