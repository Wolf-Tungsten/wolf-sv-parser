# Convert 流程技术文档

本文档按"先数据结构、后算法"的思路组织，帮助理解 Convert 如何将 SystemVerilog 转换为 GRH 表示。

---

## 目录

1. 数据结构（静态骨架）
   - 1.1 ConvertContext：共享状态
   - 1.2 PlanCache、PlanKey 与 PlanEntry
   - 1.3 ModulePlan：模块骨架
   - 1.4 LoweringPlan：降级计划
   - 1.5 WriteIntent / WriteSlice：写回意图
   - 1.6 WriteBackPlan：写回合并结果
2. 算法流程（动态处理）
   - 2.1 ConvertDriver：入口与主流程
   - 2.2 Pass1：符号收集
   - 2.3 Pass2：类型解析
   - 2.4 Pass3：读写分析
   - 2.5 Pass4：表达式降级
   - 2.6 Pass5：语句降级
   - 2.7 Pass6：写回合并
   - 2.8 Pass7：内存端口处理
   - 2.9 Pass8：图组装
3. 完整示例
   - 3.1 示例源码
   - 3.2 各阶段数据结构状态
4. 代码映射

---

## 1. 数据结构（静态骨架）

数据结构是 Convert 流程的静态骨架，保存从 AST 提取的各种信息。算法（Pass）读取、转换、写入这些数据结构，逐步将 SystemVerilog 转换为 GRH。

### 1.1 ConvertContext：共享状态

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

字段说明：
- `compilation`：访问全局类型/语义信息
- `root`：访问顶层实例列表
- `options`：转换选项（abortOnError、maxLoopIterations 等）
- `diagnostics`：诊断收集器指针
- `logger`：日志收集器指针
- `planCache`：模块计划缓存指针
- `planQueue`：模块任务队列指针

生命周期：在 ConvertDriver::convert 中栈上临时构造，传递给各 Pass。

---

### 1.2 PlanCache、PlanKey 与 PlanEntry

这三个概念共同构成模块计划的存储与索引体系。

**PlanCache** 是全局缓存，存储所有模块的计划数据。它内部维护一个哈希表，定义在 `include/convert.hpp` 第 525-552 行：

```cpp
class PlanCache {
private:
    mutable std::mutex mutex_;
    std::unordered_map<PlanKey, PlanEntry, PlanKeyHash> entries_;
};
```

**PlanKey** 是哈希表的键（第 489-510 行），用于唯一标识和查找模块实例。相同 PlanKey 的模块只处理一次，实现去重：

```cpp
struct PlanKey {
    const slang::ast::DefinitionSymbol* definition = nullptr;
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string paramSignature;
    
    bool operator==(const PlanKey& other) const noexcept;
};
```

字段说明：
- `definition`：指向模块定义符号（优先使用）
- `body`：指向实例体符号（definition 为空时使用）
- `paramSignature`：参数序列化签名（如"WIDTH=8;DEPTH=16"）

**PlanEntry** 是哈希表的值（第 519-523 行），实际存储在 PlanCache 中：

```cpp
struct PlanEntry {
    PlanStatus status = PlanStatus::Pending;
    std::optional<ModulePlan> plan;
    PlanArtifacts artifacts;
};
```

字段说明：
- `status`：处理状态（Pending / Planning / Done / Failed）
- `plan`：模块计划（ModulePlan）
- `artifacts`：阶段性产物（LoweringPlan / WriteBackPlan）

**三者的关系**：

```
PlanCache (全局单例，在 ConvertDriver 中实例化)
    └── entries_ (unordered_map)
            ├── PlanKey{adder_def, "WIDTH=8"}  →  PlanEntry{plan=adder_8bit, ...}
            ├── PlanKey{adder_def, "WIDTH=16"} →  PlanEntry{plan=adder_16bit, ...}
            └── PlanKey{top_def, ""}           →  PlanEntry{plan=top, ...}
```

**PlanEntry 状态转换**：

```
初始状态
    status = Pending
    plan = nullopt
    artifacts = empty

    ↓ tryClaim(key) 成功

处理中
    status = Planning

    ↓ storePlan(key, modulePlan) 调用

完成（ModulePlan 就绪）
    status = Done
    plan = ModulePlan{...}

    ↓ setLoweringPlan(key, loweringPlan) 调用

产物填充（LoweringPlan 就绪）
    artifacts.loweringPlan = LoweringPlan{...}

    ↓ setWriteBackPlan(key, writeBackPlan) 调用

产物填充（WriteBackPlan 就绪）
    artifacts.writeBackPlan = WriteBackPlan{...}
```

---

### 1.3 ModulePlan：模块骨架

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

字段说明：
- `body`：指向 InstanceBodySymbol（AST 入口）
- `symbolTable`：符号驻留表（string -> PlanSymbolId）
- `moduleSymbol`：模块名索引
- `ports`：端口信息列表（方向、位宽等）
- `signals`：信号信息列表（类型、维度等）
- `rwOps`：读写访问记录
- `memPorts`：内存端口记录
- `instances`：子实例记录

**例子**：端口收集过程

SystemVerilog 源码：

```systemverilog
module test (
    input  logic [7:0] a,
    output logic [7:0] y
);
```

Pass1 执行后（ModulePlan 初始状态）：

```cpp
ModulePlan plan;
plan.body = &test_body;
plan.symbolTable = {"a" -> id(0), "y" -> id(1)};
plan.moduleSymbol = id(0);  // "test"
plan.ports = {
    {symbol=id(0), direction=Input, width=0, isSigned=false},
    {symbol=id(1), direction=Output, width=0, isSigned=false}
};
```

Pass2 执行后（类型信息填充）：

```cpp
plan.ports[0].width = 8;  // 从 PortSymbol 类型解析
plan.ports[1].width = 8;
```

---

### 1.4 LoweringPlan：降级计划

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

字段说明：
- `values`：表达式节点序列（ExprNode）
- `roots`：每条 RHS 的根节点入口
- `tempSymbols`：操作节点的临时符号名
- `writes`：写回意图列表（WriteIntent）
- `loweredStmts`：按序语句列表
- `dpiImports`：DPI import 元数据
- `memoryReads`：读端口条目
- `memoryWrites`：写端口条目

**例子**：表达式降级过程

SystemVerilog 源码：

```systemverilog
assign y = (a & b) ? ~c : (a | b);
```

Pass4 逐步构建 values：

```cpp
// values[0] = Symbol("a")
values.push_back(ExprNode{kind=Symbol, symbol="a"});

// values[1] = Symbol("b")
values.push_back(ExprNode{kind=Symbol, symbol="b"});

// values[2] = Operation(kAnd, [0, 1])
values.push_back(ExprNode{
    kind=Operation,
    op=kAnd,
    operands={0, 1},
    tempSymbol="_expr_tmp_0"
});

// values[3] = Symbol("c")
values.push_back(ExprNode{kind=Symbol, symbol="c"});

// values[4] = Operation(kNot, [3])
values.push_back(ExprNode{
    kind=Operation,
    op=kNot,
    operands={3},
    tempSymbol="_expr_tmp_1"
});

// a 和 b 再次出现，生成新节点
values.push_back(ExprNode{kind=Symbol, symbol="a"});  // [5]
values.push_back(ExprNode{kind=Symbol, symbol="b"});  // [6]

// values[7] = Operation(kOr, [5, 6])
values.push_back(ExprNode{
    kind=Operation,
    op=kOr,
    operands={5, 6},
    tempSymbol="_expr_tmp_2"
});

// values[8] = Operation(kMux, [2, 4, 7])
values.push_back(ExprNode{
    kind=Operation,
    op=kMux,
    operands={2, 4, 7},
    tempSymbol="_expr_tmp_3"
});

// 记录根节点
roots = {LoweredRoot{value=8}};
```

---

### 1.5 WriteIntent / WriteSlice：写回意图

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

字段说明：
- `target`：写回目标符号（PlanSymbolId）
- `slices`：LHS 切片链路（bit-select / range-select / member-select）
- `value`：RHS 值节点索引（ExprNodeId）
- `guard`：guard 条件节点索引（ExprNodeId）
- `domain`：控制域（Combinational / Sequential / Latch / Unknown）
- `isNonBlocking`：是否非阻塞赋值
- `coversAllTwoState`：二值覆盖完整标记

**WriteSlice**：描述单次切片操作，定义在第 355-363 行

```cpp
struct WriteSlice {
    WriteSliceKind kind = WriteSliceKind::None;      // 切片类型
    WriteRangeKind rangeKind = WriteRangeKind::Simple; // 范围选择子类型
    ExprNodeId index = kInvalidPlanIndex;            // bit-select 索引
    ExprNodeId left = kInvalidPlanIndex;             // range 左边界
    ExprNodeId right = kInvalidPlanIndex;            // range 右边界
    PlanSymbolId member;                             // 成员名（member-select）
    slang::SourceLocation location{};
};
```

WriteSliceKind 枚举（第 342-347 行）：
- `None`：无切片（整信号写回）
- `BitSelect`：位选择，如 `y[3]`
- `RangeSelect`：范围选择，如 `y[7:4]` 或 `y[base +: width]`
- `MemberSelect`：成员选择，如 `struct_signal.member`

WriteRangeKind 枚举（第 349-353 行）：
- `Simple`：固定范围，如 `[7:4]`
- `IndexedUp`：向上索引，如 `[base +: width]`
- `IndexedDown`：向下索引，如 `[base -: width]`

**例子**：位选择与范围选择

SystemVerilog 源码：

```systemverilog
always_comb begin
    y[3] = a;           // BitSelect
    y[7:4] = b;         // RangeSelect (Simple)
    y[idx +: 2] = c;    // RangeSelect (IndexedUp)
end
```

生成的 writes：

```cpp
writes = {
    {
        target = "y",
        slices = {
            {kind=BitSelect, index=Constant(3)}
        },
        value = Symbol("a"),
        guard = invalid,
        domain = Combinational
    },
    {
        target = "y",
        slices = {
            {kind=RangeSelect, rangeKind=Simple, left=Constant(7), right=Constant(4)}
        },
        value = Symbol("b"),
        guard = invalid,
        domain = Combinational
    },
    {
        target = "y",
        slices = {
            {kind=RangeSelect, rangeKind=IndexedUp, index=Symbol("idx"), right=Constant(2)}
        },
        value = Symbol("c"),
        guard = invalid,
        domain = Combinational
    }
};
```

**例子**：嵌套切片（数组索引 + 成员选择）

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
{
    target = "mem",
    slices = {
        {kind=BitSelect, index=Symbol("idx")},      // 数组索引
        {kind=MemberSelect, member="hi"}            // 成员选择
    },
    value = Symbol("a"),
    guard = invalid,
    domain = Sequential,
    isNonBlocking = true
}
```

slices 按访问顺序排列，先 element select 后 member select。

**例子**：if/else 链路的写回意图生成

SystemVerilog 源码：

```systemverilog
always_comb begin
    if (a) y = b;
    else if (c) y = d;
    else y = e;
end
```

Pass5 生成的 values（guard 相关节点）：

```cpp
values = {
    [0] Symbol("b"),
    [1] Symbol("d"),
    [2] Symbol("e"),
    [3] Symbol("a"),           // 条件 a
    [4] Symbol("c"),           // 条件 c
    [5] Operation(kLogicNot, [3]),      // !a
    [6] Operation(kLogicAnd, [5, 4]),   // (!a) && c
    [7] Operation(kLogicNot, [4]),      // !c
    [8] Operation(kLogicAnd, [5, 7])    // (!a) && (!c)
};
```

生成的 writes：

```cpp
writes = {
    {
        target = "y",
        slices = {},
        value = 0,      // "b"
        guard = 3,      // "a"
        domain = Combinational
    },
    {
        target = "y",
        slices = {},
        value = 1,      // "d"
        guard = 6,      // "(!a) && c"
        domain = Combinational
    },
    {
        target = "y",
        slices = {},
        value = 2,      // "e"
        guard = 8,      // "(!a) && (!c)"
        domain = Combinational
    }
};
```

---

### 1.6 WriteBackPlan：写回合并结果

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

Entry 字段说明：
- `target`：写回目标符号
- `signal`：信号索引
- `domain`：控制域
- `updateCond`：写回触发条件（guard OR 合并）
- `nextValue`：合并后的 nextValue（按语句顺序构建 mux 链）
- `eventEdges`：顺序域事件边沿列表
- `eventOperands`：顺序域事件信号列表

**例子**：顺序写回合并

SystemVerilog 源码：

```systemverilog
always_ff @(posedge clk) begin
    if (rst) q <= 1'b0;
    else if (en) q <= d;
end
```

LoweringPlan.writes（Pass5 输出）：

```cpp
writes = {
    {target="q", value=Constant("1'b0"), guard="rst", domain=Sequential},
    {target="q", value=Symbol("d"), guard="(!rst)&&en", domain=Sequential}
};
eventEdges = {Posedge};
eventOperands = {Symbol("clk")};
```

WriteBackPlan.entries（Pass6 输出）：

```cpp
entries = {
    {
        target = "q",
        signal = 0,
        domain = Sequential,
        updateCond = Operation(kLogicOr, ["rst", "(!rst)&&en"]),
        nextValue = Operation(kMux, ["(!rst)&&en", "d", "1'b0"]),
        eventEdges = {Posedge},
        eventOperands = {Symbol("clk")}
    }
};
```

合并规则：
- updateCond = rst || ((!rst) && en)
- nextValue = ((!rst) && en) ? d : 1'b0
- 先写的 reset 优先级更高，mux 链保持语句顺序

---

## 2. 算法流程（动态处理）

算法遍历、转换、合并数据结构，完成从 AST 到 GRH 的转换。

### 2.1 ConvertDriver：入口与主流程

ConvertDriver 是 Convert 流程的入口类，封装了整个转换过程。定义在 `include/convert.hpp` 第 657-672 行：

```cpp
class ConvertDriver {
public:
    explicit ConvertDriver(ConvertOptions options = {});
    
    // 主入口函数
    grh::ir::Netlist convert(const slang::ast::RootSymbol& root);
    
    // 访问器
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
    → Pass1 (SymbolCollector) → ModulePlan (骨架)
    → Pass2 (TypeResolver)    → ModulePlan (类型填充)
    → Pass3 (RWAnalyzer)      → ModulePlan (rwOps/memPorts)
    → Pass4 (ExprLowerer)     → LoweringPlan (values/roots)
    → Pass5 (StmtLowerer)     → LoweringPlan (+writes)
    → Pass6 (WriteBack)       → WriteBackPlan (entries)
    → Pass7 (MemoryPort)      → LoweringPlan (+memoryReads/Writes)
    → Pass8 (GraphAssembly)   → Netlist/Graph (GRH)
```

---

### 2.2 Pass1：符号收集

**输入**：InstanceBodySymbol（slang AST）

**输出**：填充骨架的 ModulePlan

**算法步骤**：

1. 初始化 ModulePlan，绑定 body 指针
2. 遍历 body.getPortList()，为每个 PortSymbol 创建 PortInfo
3. 遍历 body.members()，为 NetSymbol/VariableSymbol 创建 SignalInfo
4. 遍历实例符号（InstanceSymbol/InstanceArraySymbol），创建 InstanceInfo
5. 递归处理 GenerateBlock，将发现的子模块投递到 PlanTaskQueue

**例子**：端口收集的数据结构更新

遍历前：

```cpp
ModulePlan plan;
plan.ports = {};  // 空
```

遍历 body.getPortList() 发现端口 a：

```cpp
PortInfo info;
info.symbol = plan.symbolTable.intern("a");
info.direction = Input;  // 从 PortSymbol 获取
info.width = 0;          // Pass2 填充
plan.ports.push_back(info);
```

遍历后：

```cpp
plan.ports = {
    {symbol="a", direction=Input, width=0},
    {symbol="y", direction=Output, width=0}
};
```

---

### 2.3 Pass2：类型解析

**输入**：ModulePlan（Pass1 输出）

**输出**：原地更新 ModulePlan（填充 width/isSigned/memoryRows 等）

**算法步骤**：

1. 根据 symbolTable 建立端口/信号名到索引的映射
2. 回扫 PortSymbol，获取 Type，计算位宽和符号性
3. 回扫 NetSymbol/VariableSymbol：
   - 剥离 type alias
   - 收集 unpacked 维度，累乘计算 memoryRows
   - 收集 packed 维度到 packedDims
   - 计算元素类型宽度

**例子**：位宽计算的数据结构更新

输入 ModulePlan：

```cpp
signals = {
    {symbol="mem", kind=Variable, width=0, memoryRows=0}
};
```

发现类型为 `logic [7:0] mem [0:15]`：

```cpp
// 收集 unpacked 维度
UnpackedDimInfo dim;
dim.extent = 16;   // range.fullWidth()
dim.left = 0;
dim.right = 15;
signals[0].unpackedDims = {dim};
signals[0].memoryRows = 16;

// 收集 packed 维度并计算宽度
signals[0].packedDims = {8};
signals[0].width = 8;
signals[0].isSigned = false;
```

---

### 2.4 Pass3：读写分析

**输入**：ModulePlan + 过程块/连续赋值 AST

**输出**：原地更新 ModulePlan.rwOps 和 ModulePlan.memPorts

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

遍历前：

```cpp
rwOps = {};
```

访问者遍历：

```cpp
// 访问 NamedValueExpression "d" 在 RHS
recordRead("d", Sequential, location);

// 访问 NamedValueExpression "en" 在条件
recordRead("en", Sequential, location);

// 访问 NamedValueExpression "q" 在 LHS
recordWrite("q", Sequential, location);
```

遍历后：

```cpp
rwOps = {
    {target="d", domain=Sequential, isWrite=false, sites=[@d]},
    {target="en", domain=Sequential, isWrite=false, sites=[@en]},
    {target="q", domain=Sequential, isWrite=true, sites=[@q]}
};
```

---

### 2.5 Pass4：表达式降级

**输入**：ModulePlan + 表达式 AST

**输出**：LoweringPlan（values/roots/tempSymbols）

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

调用栈：

```cpp
lowerExpression("a + b")
    → node.op = kAdd
    → lowerExpression("a")
        → node.kind = Symbol, node.symbol = "a"
        → values[0] = node, return 0
    → lowerExpression("b")
        → node.kind = Symbol, node.symbol = "b"
        → values[1] = node, return 1
    → node.operands = {0, 1}
    → values[2] = node
    → roots.push_back(LoweredRoot{value=2})
    → return 2
```

最终 LoweringPlan：

```cpp
values = {
    [0] Symbol("a"),
    [1] Symbol("b"),
    [2] Operation(kAdd, [0, 1], tempSymbol="_expr_tmp_0")
};
roots = {LoweredRoot{value=2}};
```

---

### 2.6 Pass5：语句降级

**输入**：ModulePlan + 语句 AST + LoweringPlan（来自 Pass4）

**输出**：更新的 LoweringPlan（追加 guard 节点、WriteIntent、LoweredStmt）

**核心机制**：Guard 栈

- `guardStack` 维护当前控制路径的 guard 链
- 进入 if-true 分支：push(guard && cond)
- 进入 if-false 分支：push(guard && !cond)
- 退出分支：pop()

**例子**：if/else 的 guard 传播

SystemVerilog 源码：

```systemverilog
if (a) begin
    y = b;
end else begin
    y = c;
end
```

算法执行过程：

```cpp
// 初始状态
guardStack = {};  // 空，无条件

// 处理 if (a)
baseGuard = currentGuard();  // kInvalid（无条件）
condA = lowerExpression("a");  // values[0] = Symbol("a")
trueGuard = combineGuard(baseGuard, condA);  // "a"
pushGuard(trueGuard);  // guardStack = {"a"}

// 处理 y = b
valueB = lowerExpression("b");  // values[1] = Symbol("b")
guardB = currentGuard();  // "a"
writes.push_back({target="y", value=1, guard="a"});

popGuard();  // guardStack = {}

// 处理 else
notA = makeLogicNot(condA);  // values[2] = Operation(kLogicNot, [0])
falseGuard = combineGuard(baseGuard, notA);  // "!a"
pushGuard(falseGuard);  // guardStack = {"!a"}

// 处理 y = c
valueC = lowerExpression("c");  // values[3] = Symbol("c")
guardC = currentGuard();  // "!a"
writes.push_back({target="y", value=3, guard="!a"});

popGuard();  // guardStack = {}
```

---

### 2.7 Pass6：写回合并

**输入**：ModulePlan + LoweringPlan

**输出**：WriteBackPlan

**合并规则**：

1. **分组**：按 (target + domain + eventEdges + eventOperands) 分组
2. **Guard 合并**：updateCond = OR(guard_i)
3. **NextValue 合并**：按语句顺序构建 mux 链
   - next = mux(guard_n, value_n, ... , mux(guard_1, value_1, base))

**例子**：多条件写回合并

SystemVerilog 源码：

```systemverilog
always_ff @(posedge clk) begin
    if (rst) q <= 0;
    else if (en) q <= d;
    else if (load) q <= data;
end
```

输入 writes（按出现顺序）：

```cpp
writes = {
    {target="q", value="0",    guard="rst"},
    {target="q", value="d",    guard="(!rst)&&en"},
    {target="q", value="data", guard="(!rst)&&(!en)&&load"}
};
```

合并过程：

```cpp
// 分组：三条写入同一组（target=q, domain=Sequential, event=posedge clk）

// Guard 合并
updateCond = "rst" || "(!rst)&&en" || "(!rst)&&(!en)&&load";

// NextValue 合并（按语句顺序）
// val0 = base（隐含的旧值）
// val1 = mux(guard0, value0, val0) = mux("rst", "0", base)
// val2 = mux(guard1, value1, val1) = mux("(!rst)&&en", "d", val1)
// val3 = mux(guard2, value2, val2) = mux("(!rst)&&(!en)&&load", "data", val2)
nextValue = mux("(!rst)&&(!en)&&load", "data",
                mux("(!rst)&&en", "d",
                    mux("rst", "0", base)));
```

---

### 2.8 Pass7：内存端口处理

**输入**：ModulePlan.memPorts + LoweringPlan

**输出**：更新 LoweringPlan.memoryReads 和 LoweringPlan.memoryWrites

**算法步骤**：

1. 从 loweredStmts 中识别 mem[addr] 读访问
2. 从 writes 中识别 mem[addr] 写访问
3. 对读访问生成 MemoryReadPort
4. 对写访问生成 MemoryWritePort（支持 mask）
5. 多维 memory 地址线性化

**例子**：内存读写识别

SystemVerilog 源码：

```systemverilog
logic [7:0] mem [0:15];
assign rdata = mem[addr];              // 读
always_ff @(posedge clk)
    if (en) mem[addr] <= wdata;        // 写
```

生成的 memoryReads：

```cpp
memoryReads = {
    {
        memory = "mem",
        address = Symbol("addr"),
        data = Operation(kSliceDynamic, [mem, addr]),
        isSync = false
    }
};
```

生成的 memoryWrites：

```cpp
memoryWrites = {
    {
        memory = "mem",
        address = Symbol("addr"),
        data = Symbol("wdata"),
        mask = Constant("8'hFF"),  // 全写
        updateCond = Symbol("en"),
        eventEdges = {Posedge},
        eventOperands = {Symbol("clk")}
    }
};
```

---

### 2.9 Pass8：图组装

**输入**：ModulePlan + LoweringPlan + WriteBackPlan + Netlist

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

输入 WriteBackPlan.Entry：

```cpp
{
    target = "q",
    updateCond = "en",
    nextValue = "d",
    domain = Sequential,
    eventEdges = {Posedge},
    eventOperands = {Symbol("clk")}
}
```

发射的 GRH Operation：

```cpp
Operation kRegister {
    kind = kRegister,
    operands = {updateCond, nextValue, clk},  // 实际为 ValueId
    results = {Value("q")},
    attrs = {
        eventEdge = "posedge"
    }
}
```

---

## 3. 完整示例

### 3.1 示例源码

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

### 3.2 各阶段数据结构状态

#### Pass1 后：ModulePlan

```cpp
ModulePlan plan;
plan.body = &counter_body;
plan.symbolTable = {"clk", "rst", "en", "count"};
plan.moduleSymbol = "counter";

plan.ports = {
    {symbol="clk",   direction=Input,  width=0},
    {symbol="rst",   direction=Input,  width=0},
    {symbol="en",    direction=Input,  width=0},
    {symbol="count", direction=Output, width=0}
};

plan.signals = {};
plan.rwOps = {};
plan.memPorts = {};
plan.instances = {};
```

#### Pass2 后：类型填充

```cpp
plan.ports = {
    {symbol="clk",   direction=Input,  width=1,  isSigned=false},
    {symbol="rst",   direction=Input,  width=1,  isSigned=false},
    {symbol="en",    direction=Input,  width=1,  isSigned=false},
    {symbol="count", direction=Output, width=4,  isSigned=false}
};
```

#### Pass3 后：rwOps

```cpp
plan.rwOps = {
    {target="rst",   domain=Sequential, isWrite=false, sites=[@line7]},
    {target="en",    domain=Sequential, isWrite=false, sites=[@line9]},
    {target="count", domain=Sequential, isWrite=false, sites=[@line9]},  // RHS
    {target="count", domain=Sequential, isWrite=true,  sites=[@line8, @line9]}  // LHS
};
```

#### Pass4 后：LoweringPlan

```cpp
LoweringPlan lowering;

lowering.values = {
    [0] Symbol("rst"),
    [1] Constant("4'b0"),
    [2] Symbol("en"),
    [3] Symbol("count"),
    [4] Constant("4'b1"),
    [5] Operation(kAdd, [3, 4], tempSymbol="_expr_tmp_0")  // count + 1
};

lowering.roots = {
    LoweredRoot{value=1},  // 4'b0 (rst 分支)
    LoweredRoot{value=5}   // count + 1 (en 分支)
};
```

#### Pass5 后：LoweredStmt + writes

```cpp
// 新增 guard 相关节点
lowering.values[6] = Symbol("rst");
lowering.values[7] = Symbol("en");
lowering.values[8] = Operation(kLogicNot, [6]);        // !rst
lowering.values[9] = Operation(kLogicAnd, [8, 7]);     // (!rst) && en

lowering.writes = {
    {target="count", value=1, guard=6, domain=Sequential, isNonBlocking=true},
    {target="count", value=5, guard=9, domain=Sequential, isNonBlocking=true}
};

lowering.eventContext = {
    edgeSensitive = true,
    edges = {Posedge},
    operands = {Symbol("clk")}
};

lowering.loweredStmts = {
    {kind=Write, write=writes[0], eventEdges={Posedge}, eventOperands={clk}},
    {kind=Write, write=writes[1], eventEdges={Posedge}, eventOperands={clk}}
};
```

#### Pass6 后：WriteBackPlan

```cpp
WriteBackPlan writeBack;

writeBack.entries = {
    {
        target = "count",
        signal = 0,
        domain = Sequential,
        updateCond = Operation(kLogicOr, [6, 9]),  // rst || ((!rst) && en)
        nextValue = Operation(kMux, [9, 5, 1]),    // en ? (count+1) : 0
        eventEdges = {Posedge},
        eventOperands = {Symbol("clk")}
    }
};
```

#### Pass7 后：无变化（无 memory 访问）

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

## 4. 代码映射

### 4.1 文件位置

- 数据结构定义：`include/convert.hpp`
- Pass 实现：`src/convert.cpp`
- 测试用例：`tests/data/convert/*.sv`
- 测试代码：`tests/convert/test_convert_*.cpp`

### 4.2 关键定义位置

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

### 4.3 函数入口

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
