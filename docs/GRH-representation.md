# Graph RTL Hierarchy（GRH）Representation

借鉴 MLIR SSA 形式的描述，融合 RTL 设计特点定制的表示方法。

GRH 表示提供一种基于图的分析、操作 RTL 的入口。

GRH 表示在编译流程中的功能定位如下：

高层次行为化的 SystemVerilog - slang AST - **GRH 表示** - 低层次网表化的 SystemVerilog

上述过程可视为 RTL-to-RTL 的变换，变换过程应当保持功能等价

变换过程中，GRH表示尽可能避免对RTL语义的分析，语义应当由处理低层次 SystemVerilog 的仿真器、逻辑综合工具解释。

典型的案例是 GRH 不关心各种操作数、地址位宽之间的匹配规则，如果出现截断或者越界，应当由处理低层次SystemVerilog的工具处理。



# GRH 表示的层次结构（自底向上）

- 顶点（Operation）和超边（Value）
    - 顶点：用 Operation 建模组合逻辑、寄存器、模块实例化、调试、DPI；
    - 超边：用 Value 建模操作、寄存器、模块实例化之间的连接，Value 满足静态单赋值特点，因此是单源多输出的超边；
- 图（Graph）
    - 作为 Operation 和 Value 的容器，包含一组关联的顶点和边
    - 每个图建模、生成一个 SystemVerilog Module
- 网表（Netlist）
    - 是图的集合
    - 存在一个或多个图作为顶层模块

# 符号表与句柄

- GRH API 以 `SymbolId` 作为符号句柄，通过 `GraphSymbolTable`/`NetlistSymbolTable` 驻留字符串
- `ValueId/OperationId/GraphId` 为强类型句柄（index + generation + GraphId），用于跨图校验；`invalid` 表示无效
- `SymbolId`/`ValueId`/`OperationId` 只在各自符号表或 Graph 生命周期内稳定，不保证跨进程持久化

# 顶点 - Operation

- Operation 具有类型：由枚举 `OperationKind` 表示，类型有限且预定义
- Operation 可被符号索引：使用 `SymbolId`（由 `GraphSymbolTable` 驻留的字符串），在 Graph 作用域内建议唯一；允许为空（invalid）
- Operation 接受操作数作为输入：`operands` 为 `ValueId` 列表
- Operation 输出结果：`results` 为 `ValueId` 列表
- Operation 的属性：`attrs` 为 `AttrKV` 列表（`{SymbolId key, AttributeValue value}`）
    - key 由图内符号表驻留
    - value 允许的类型
        - 基本类型：`bool`、`int64_t`、`double`、`std::string`
        - 包含基本类型的数组：`std::vector<basic_type>`
- 只读访问通过 `Graph::getOperation(OperationId)` 或 `GraphView` 提供；修改必须经过 `Graph/GraphBuilder` 接口


## Operation 类型概览

- 常量：`kConstant`
- 组合逻辑：`kAdd`、`kSub`、`kMul`、`kDiv`、`kMod`、`kEq`、`kNe`、`kCaseEq`、`kCaseNe`、`kWildcardEq`、`kWildcardNe`、`kLt`、`kLe`、`kGt`、`kGe`、`kAnd`、`kOr`、`kXor`、`kXnor`、`kNot`、`kLogicAnd`、`kLogicOr`、`kLogicNot`、`kReduceAnd`、`kReduceOr`、`kReduceXor`、`kReduceNor`、`kReduceNand`、`kReduceXnor`、`kShl`、`kLShr`、`kAShr`、`kMux`
- 连线：`kAssign`、`kConcat`、`kReplicate`、`kSliceStatic`、`kSliceDynamic`、`kSliceArray`
- 时序：`kLatch`、`kLatchArst`、`kRegister`、`kRegisterEn`、`kRegisterRst`、`kRegisterEnRst`、`kRegisterArst`、`kRegisterEnArst`、`kMemory`、`kMemoryAsyncReadPort`、`kMemorySyncReadPort`、`kMemorySyncReadPortRst`、`kMemorySyncReadPortArst`、`kMemoryWritePort`、`kMemoryMaskWritePort`
- 层次：`kInstance`、`kBlackbox`
- 调试：`kDisplay`、`kAssert`
- DPI：`kDpicImport`、`kDpicCall`

# 边 - Value

满足静态单赋值SSA特性，只能由一个 Operation 写入，可以被多个 Operation 读取。

每个 Value 最终都会显式生成为 SystemVerilog 中的 wire 声明，以支持 SSA 特性。

- 具有一个 `SymbolId` 类型的 symbol 字段，用于识别信号，符号来自 `GraphSymbolTable`，在 Graph 作用域内建议唯一且非空；`Graph::internSymbol()` 负责驻留字符串
- 具有一个 `int32_t` 类型的 width 字段，表示 Value 的位宽，`width` 必须大于 0
- 具有一个 bool 类型 signed 标记是否为有符号
- 支持 SystemVerilog 四态逻辑（0/1/x/z），Value 与 Operation 的语义均按四态传播；常量允许使用 x/z 字面量
- Value 数据类型对数组和结构体进行扁平化，对于数组和结构体的读写操作通过 kSlice 和 kConcat 实现，不能破坏SSA特性。扁平化顺序遵循 SystemVerilog 的 packed array 和结构体布局规则：同一层级内自左向右（MSB→LSB）展开，多维数组先按最高维（左侧索引）递增，再在每个元素内部继续按 MSB→LSB 展开。

生成语义

```
wire ${signed ? "signed" : ""} [${width}-1:0] ${symbol};
```

记录是否作为模块端口

- 具有一个标记是否为模块输入的布尔字段 isInput
- 具有一个标记是否为模块输出的布尔字段 isOutput
- 具有一个标记是否为模块 inout 的布尔字段 isInout
- 以上二者不能同时为真
- 当 `isInput == true` 时，该 Value 必须被绑定在所属 Graph 的 `inputPorts` 中；同理，`isOutput == true` 的 Value 必须被绑定在 `outputPorts` 中
- 当 `isInout == true` 时，该 Value 必须出现在所属 Graph 的 `inoutPorts` 中（作为 `in/out/oe` 之一）
- 未绑定端口的 Value 必须满足 `isInput == false` 且 `isOutput == false` 且 `isInout == false`
- inout 端口对应的 `__in/__out/__oe` Value 仅设置 `isInout`，不使用 `isInput/isOutput` 标记，端口关系由 `inoutPorts` 表驱动

辅助字段

- 具有一个 `ValueId` 句柄（index + generation + GraphId），用于跨 API 传递与跨图校验
- 具有一个 defineOp 字段，类型为 `OperationId`，指向写入 Op；若 Value 是模块的输入参数，则为 invalid
- 具有一个 users 数组，元素为 `ValueUser{OperationId operation, uint32_t operandIndex}` 二元组，记录该 Value 在各 Operation 中作为第几个操作数被引用；同一 Operation 多次使用同一个 Value 时会存储多个条目
- Graph 创建 Value 时即拥有其生命周期，禁止在多个 Graph 之间共享 ValueId；`OperationId/ValueId` 内含 GraphId 用于运行时校验

# 图 - Graph

建模一个参数化后的 module：

- 具有一个 graph symbol（module 名称），符合 verilog 标识符规范，在网表中唯一，并由 `NetlistSymbolTable` 分配 `GraphId`
- 具有一个 `GraphSymbolTable` 管理图内符号，`Graph::internSymbol/lookupSymbol/symbolText` 为主要入口
- 端口分为 input/output/inout：input/output 以 `std::vector<Port>` 存储，元素为 `{SymbolId name, ValueId value}`；inout 以 `std::vector<InoutPort>` 存储，元素为 `{SymbolId name, ValueId in, ValueId out, ValueId oe}`；`bindInputPort/bindOutputPort/bindInoutPort` 负责绑定
- Graph 内部采用 `GraphBuilder`（可变）与 `GraphView`（只读快照）双态；`freeze()` 生成只读视图并可能重排 id
- `ValueId/OperationId` 的 index 仅在同一构建期会话内稳定，跨 `freeze()` 需要重新获取
- inout 采用 3-value 模型，读/写/使能为独立 Value，禁止单个 Value 同时作为 input/output 以保持 SSA
- inout 相关 Value 命名约定为 `BASE__in/__out/__oe`，`BASE` 与端口名对应

生成语义

```
module ${graphSymbol} (
    ${CommaSeparatedList(
        for (const auto& port : inputPorts)
            -> "input "  + (value(port.value).signed ? "signed " : "")
               + "[" + std::to_string(value(port.value).width - 1) + ":0] " + symbolText(port.name),
        for (const auto& port : outputPorts)
            -> "output " + (value(port.value).signed ? "signed " : "")
               + "[" + std::to_string(value(port.value).width - 1) + ":0] " + symbolText(port.name),
        for (const auto& port : inoutPorts)
            -> "inout "  + (value(port.out).signed ? "signed " : "")
               + "[" + std::to_string(value(port.out).width - 1) + ":0] " + symbolText(port.name)
    )}
);
    ${for (const auto& port : inoutPorts)
        -> "assign " + valueSymbol(port.in) + " = " + symbolText(port.name) + ";"
           "assign " + symbolText(port.name) + " = " + valueSymbol(port.oe) + " ? "
               + valueSymbol(port.out) + " : {" + std::to_string(value(port.out).width) + "{1'bz}};"
    }
    由 Graph 内部的 Operation 和 Value 生成的语句
endmodule
```

其中 `value(...)` 表示通过 Graph 访问 Value 元数据，`symbolText(...)` 用于从 `SymbolId` 取回字符串；`CommaSeparatedList` 依次枚举 inputPorts、outputPorts、inoutPorts；在冻结后的 `GraphView` 中，端口按端口名的字典序排序（构建期插入顺序不保证保持原始声明顺序）。

Graph 管理 Operation 和 Value 的生命周期：

- Graph 内部存储由 `GraphBuilder/GraphView` 管理，外部仅持有 `ValueId/OperationId` 句柄
- `Graph::getValue/Graph::getOperation` 返回只读快照，修改必须通过 `Graph` 接口完成
- `operations()`/`values()` 返回创建顺序的 id 列表（跳过已删除），不保证拓扑顺序

## 端口与 JSON 序列化

Graph JSON 序列化中，端口字段位于 `graph.ports`：

- `ports.in`：输入端口数组，每个元素包含 `name` 与 `val`
- `ports.out`：输出端口数组，每个元素包含 `name` 与 `val`
- `ports.inout`：inout 端口数组，每个元素包含 `name/in/out/oe`

约束：

- `ports.in/out` 的 `val` 必须指向 `vals` 中存在的 Value symbol
- `ports.inout` 的 `in/out/oe` 必须指向 `vals` 中存在的 Value symbol
- `in/out/oe` 的位宽需匹配端口位宽（建议 `oe` 与 `out` 等宽）
- inout 三个 Value 在 `vals` 中的 `in/out` 标记必须为 false
- inout 三个 Value 在 `vals` 中的 `inout` 标记必须为 true，由 `ports.inout` 提供端口语义

示例：
```json
{
  "ports": {
    "in": [
      { "name": "i_clk", "val": "i_clk" }
    ],
    "out": [
      { "name": "o_led", "val": "o_led" }
    ],
    "inout": [
      { "name": "pad", "in": "pad__in", "out": "pad__out", "oe": "pad__oe" }
    ]
  }
}
```

# 网表 - Netlist

- 具有一个可按 graph symbol 索引 Graph 的容器，要求 symbol 在网表中唯一，并保留 `graphOrder` 记录创建顺序
- Graph 名称由 `NetlistSymbolTable` 驻留并分配 `GraphId`；Graph 内符号与 Netlist 符号表相互独立
- 顶层模块由 `markAsTop()` 维护 `topGraphs` 列表，不再依赖 Graph 内部标记
- instance Operation 的 attributes 中记录被例化模块的 moduleName，运行时通过 Netlist 解析被例化 Graph；禁止跨网表引用
- 允许存在未被引用的 Graph（如库模块），但综合或导出流程默认仅从顶层 Graph 开始遍历可达子图
- 别名通过 `registerGraphAlias(alias, graph)` 维护，`findGraph` 会解析 alias
- 层次结构由 Graph 内的 instance Operation 建模，网表自身不额外存储层次边

# Operation 分类详解

## 常量定义操作 kConstant

- operands：无
- results：
    - res：产生的常量值
- attributes：
    - constValue：string 类型，使用 verilog 常量语法记录常量值，例如 `8'hEF`, `16'sd-5`

生成语义：
```
assign ${res.symbol} = ${constValue}

```

## 组合逻辑操作

### 四态语义约定

- 值域：bit 取值 `{0, 1, X, Z}`，常量允许 `x/z` 字面量。
- 约定：除比较类操作外，`Z` 默认按 `X` 处理（保守传播）。
- `kEq`/`kNe`：对应 `==/!=`，若任一位含 `X/Z` 且不形成确定不等，则结果为 `X`。
- `kCaseEq`/`kCaseNe`：对应 `===/!==`，按位全等比较，结果恒为 `0/1`。
- `kWildcardEq`/`kWildcardNe`：对应 `==?/!=?`，任一操作数的 `X/Z` 视为 wildcard，结果恒为 `0/1`。
- 位运算（`kAnd/kOr/kXor/kXnor/kNot`）：遵循 SV 四态真值表；`0`/`1` 优先确定，无法确定时返回 `X`。
- 逻辑运算（`kLogicAnd/kLogicOr/kLogicNot`）：先将操作数规约为 1-bit 逻辑值 `{0,1,X}` 再计算，`X` 保持传播。
- 归约运算（`kReduce*`）：任一位含 `X/Z` 且无法确定结果时返回 `X`。
- 算术/移位（`kAdd`/`kSub`/`kMul`/`kDiv`/`kMod`/`kShl`/`kLShr`/`kAShr`）：任一操作数含 `X/Z` 时结果为全 `X`（保守语义）。
- `kMux`：`sel=1` 取真分支，`sel=0` 取假分支；`sel=X/Z` 时逐位融合（`a[i]==b[i]` 则取该值，否则为 `X`）。

### 二元操作符

包含 `kAdd`(+)、`kSub`(-)、`kMul`(*)、`kDiv`(/)、`kMod`(%)、`kEq`(==)、`kNe`(!=)、`kCaseEq`(===)、`kCaseNe`(!==)、`kWildcardEq`(==?)、`kWildcardNe`(!=?)、`kLt`(<)、`kLe`(<=)、`kGt`(>)、`kGe`(>=)、`kAnd`(&)、`kOr`(|)、`kXor`(^)、`kXnor`(~^)、`kLogicAnd`(&&)、`kLogicOr`(||)、`kShl`(<<)、`kLShr`(>>)、`kAShr`(>>>)。

- operands：
    - op0：左侧操作数
    - op1：右侧操作数
- results：
    - res：操作结果

生成语义：
<operator> 指代 verilog 操作符，例如 kAdd 的 <operator> 为 +
```
assign ${res.symbol} = ${op0.symbol} <operator> ${op1.symbol};
```

### 一元操作符

包含`kNot`(~)、`kLogicNot`(!)、`kReduceAnd`(&)、`kReduceOr`(|)、`kReduceXor`(^)、`kReduceNor`(~|)、`kReduceNand`(~&)、`kReduceXnor`(~^)。

- operands：
    - op：右侧操作数
- results：
    - res：操作结果

生成语义：
<operator> 指代 verilog 操作符，例如 kNot 的 <operator> 为 ~
```
assign ${res.symbol} = <operator> ${op.symbol};
```

### 三元操作符 kMux

- operands：
    - op0：选择条件
    - op1：为真时结果
    - op2：为假时结果
- result：
    - res：选择结果

生成语义：
```
assign ${res.symbol} = ${op0.symbol} ? ${op1.symbol} : ${op2.symbol};
```

## 连线操作

### 连续赋值操作 kAssign

- operands：
    - input：待赋值的输入信号
- results：
    - res：赋值后的输出结果

生成语义：
```
assign ${res.symbol} = ${input.symbol};
```

### 拼接操作符 kConcat

- operands：可变数量的输入信号，至少包含两个元素，按插入顺序排列
- results：
    - res：操作结果

生成语义：
```
assign ${res.symbol} = {${CommaSeparatedList(
        for (ValueId operand : operands)
            -> valueSymbol(operand)
    )}};
```

### 复制操作符 kReplicate

- operand：
    - op：待复制操作数
- attribute：
    - rep：int64_t 类型，表示复制数量
- result：
    - res：操作结果

生成语义：
```
assign ${res.symbol} = {${rep}{op.symbol}};
```


### 静态位截取操作 kSliceStatic

- operand：
    - input：待截取的输入信号
- attributes：
    - sliceStart：int64_t 类型，表示起始位（含），按照 LSB=0 计数
    - sliceEnd：int64_t 类型，表示结束位（含），要求 `sliceEnd >= sliceStart`
- result：
    - res：操作结果

生成语义：
```
// 更偏向 Verilog 的紧凑写法：单比特切片用 bit-select，多比特用 range-select。
assign ${res.symbol} = (sliceEnd == sliceStart)
    ? ${input.symbol}[${sliceStart}]
    : ${input.symbol}[${sliceEnd}:${sliceStart}];
```

GRH 的 Value 不保留结构体、数组结构语义，但需要支持数组和结构体的访问，使用 kSlice* 系列操作符实现。

### 动态位截取操作 kSliceDynamic

- operands：
    - input：待截取的输入信号
    - offset：截取的起始偏移，按无符号数解释
- attributes：
    - sliceWidth：int64_t 类型，表示截取的位宽，必须大于 0
- result：
    - res：操作结果

生成语义：
```
assign ${res.symbol} = ${input.symbol}[${offset.symbol} +: ${sliceWidth}];
```

### 数组位截取操作 kSliceArray
- operands：
    - input：扁平化后的数组输入信号
    - index：访问的数组下标，按无符号数解释
- attributes：
    - sliceWidth：int64_t 类型，表示单个数组元素的位宽，必须大于 0 且整除 `input` 的位宽
- result：
    - res：操作结果

生成语义：
```
// 单比特数组元素用 bit-select，多比特保持 indexed part-select。
assign ${res.symbol} = (sliceWidth == 1)
    ? ${input.symbol}[${index.symbol}]
    : ${input.symbol}[${index.symbol} * ${sliceWidth} +: ${sliceWidth}];
```

GRH 支持多维数组，但不记录多维数组的层次结构，当访问多维数组时通过 kSliceArray 级联实现。

## 时序逻辑操作
### 电平敏感锁存器 kLatch

kLatch 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - en：使能信号，必须为 1 bit
    - d：数据输入
- result：
    - q：锁存器输出
- attributes：
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always_latch begin
    if (en_active) begin
        ${symbol} = ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

### 带异步复位的锁存器 kLatchArst

kLatchArst 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - en：使能信号，必须为 1 bit
    - rst：异步复位信号，必须为 1 bit
    - resetValue：复位值，位宽需与数据输入一致
    - d：数据输入
- result：
    - q：锁存器输出
- attributes：
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）
    - rstPolarity：string 类型，取值 `high` / `low`，指明复位信号的有效极性

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always_latch begin
    if (rst_active) begin
        ${symbol} = ${resetValue.symbol};
    end else if (en_active) begin
        ${symbol} = ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

### 无复位寄存器 kRegister

kRegister 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - clk：时钟信号
    - d：数据输入
- result：
    - q：寄存器输出
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿

生成语义：
```
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    ${symbol} <= ${d.symbol};
end
assign ${q.symbol} = ${symbol};
```

### 同步复位寄存器 kRegisterRst

kRegisterRst 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - clk：时钟信号
    - rst：复位信号
    - resetValue：复位值
    - d：数据输入
- result：
    - q：寄存器输出
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - rstPolarity：string 类型，取值 `high` / `low`，指明复位信号的有效极性（Active high/Active low）

生成语义：
```
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    if (rst_active) begin
        ${symbol} <= ${resetValue.symbol};
    end else begin
        ${symbol} <= ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

### 异步复位寄存器 kRegisterArst

kRegisterArst 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - clk：时钟信号
    - rst：复位信号
    - resetValue：复位值
    - d：数据输入
- result：
    - q：寄存器输出
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - rstPolarity：string 类型，取值 `high` / `low`，指明复位信号的有效极性（Active high/Active low）

生成语义：
```
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol} or ${rstEdge} ${rst.symbol}) begin
    if (rst_active) begin
        ${symbol} <= ${resetValue.symbol};
    end else begin
        ${symbol} <= ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

其中 `rstEdge = (rstPolarity == "high") ? "posedge" : "negedge"`。

### 带使能寄存器 kRegisterEn

kRegisterEn 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。使能信号的极性由 `enLevel` attribute 指定。

- operands：
    - clk：时钟信号
    - en：使能信号
    - d：数据输入
- result：
    - q：寄存器输出
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    if (en_active) begin
        ${symbol} <= ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

### 带使能同步复位寄存器 kRegisterEnRst

kRegisterEnRst 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。使能信号的极性由 `enLevel` attribute 指定。

- operands：
    - clk：时钟信号
    - rst：复位信号
    - en：使能信号
    - resetValue：复位值
    - d：数据输入
- result：
    - q：寄存器输出
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - rstPolarity：string 类型，取值 `high` / `low`，指明复位信号的有效极性
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    if (rst_active) begin
        ${symbol} <= ${resetValue.symbol};
    end else if (en_active) begin
        ${symbol} <= ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

### 带使能异步复位寄存器 kRegisterEnArst

kRegisterEnArst 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。使能信号的极性由 `enLevel` attribute 指定。

- operands：
    - clk：时钟信号
    - rst：复位信号
    - en：使能信号
    - resetValue：复位值
    - d：数据输入
- result：
    - q：寄存器输出
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - rstPolarity：string 类型，取值 `high` / `low`，指明复位信号的有效极性
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${d.signed ? "signed" : ""} [${d.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol} or ${rstEdge} ${rst.symbol}) begin
    if (rst_active) begin
        ${symbol} <= ${resetValue.symbol};
    end else if (en_active) begin
        ${symbol} <= ${d.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

其中 `rstEdge = (rstPolarity == "high") ? "posedge" : "negedge"`。

### 片上存储器 kMemory

kMemory 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operand: 无
- result: 无
- attributes：
    - width（int64_t）：每一行（word）的 bit 宽度
    - row（int64_t）：总行数，决定寻址空间
    - isSigned（bool）：标记存储内容是否为有符号数

生成语义：
```
reg ${isSigned ? "signed" : ""} [${width}-1:0] ${symbol} [0:${row}-1];
```

### 片上存储器异步读端口 kMemoryAsyncReadPort

- operands：
    - addr：读地址信号
- results：
    - data：读数据输出信号
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol。该 symbol 必须在当前 Graph 内解析到一个 kMemory Operation，生成语义时可基于该 Operation 的位宽、符号属性等信息。

生成语义：
```
assign ${data.symbol} = ${memSymbol}[${addr.symbol}];
```

### 片上存储器同步读端口 kMemorySyncReadPort

kMemorySyncReadPort 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

读数据经过端口内部寄存器 `${symbol}` 输出；复位版本仅作用于该读寄存器，不影响 memory 内容。

- operands：
    - clk：读时钟信号
    - addr：读地址信号
    - en：读使能信号
- results：
    - data：读数据输出信号
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - memSymbol：指向目标 kMemory 的 symbol。该 symbol 必须在当前 Graph 内解析到一个 kMemory Operation，生成语义中访问的 `memSymbol.isSigned`、`memSymbol.width` 等字段均来源于该 Operation。
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
reg ${memSymbol.isSigned ? "signed" : ""} [${memSymbol.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    if (en_active) begin
        ${symbol} <= ${memSymbol}[${addr.symbol}];
    end
end
assign ${data.symbol} = ${symbol};
```

### 片上存储器同步读端口（同步复位） kMemorySyncReadPortRst

复位仅作用于读寄存器 `${symbol}`，不改写 `memSymbol` 的存储内容。

- operands：
    - clk：读时钟信号
    - rst：复位信号
    - addr：读地址信号
    - en：读使能信号
- results：
    - data：读数据输出信号
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge
    - rstPolarity：string 类型，取值 `high` / `low`
    - enLevel：string 类型，取值 `high` / `low`（默认 high）
    - memSymbol：指向目标 kMemory 的 symbol

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${memSymbol.isSigned ? "signed" : ""} [${memSymbol.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    // 复位作用于读寄存器，rst_active 时的复位值由 elaboration 显式提供
    if (en_active) begin
        ${symbol} <= ${memSymbol}[${addr.symbol}];
    end
end
assign ${data.symbol} = ${symbol};
```

### 片上存储器同步读端口（异步复位） kMemorySyncReadPortArst

复位仅作用于读寄存器 `${symbol}`，不改写 `memSymbol` 的存储内容。

- operands：
    - clk：读时钟信号
    - rst：复位信号
    - addr：读地址信号
    - en：读使能信号
- results：
    - data：读数据输出信号
- attributes：
    - clkPolarity：string 类型，取值 posedge / negedge
    - rstPolarity：string 类型，取值 `high` / `low`，决定 `rst` 的有效极性及敏感沿
    - enLevel：string 类型，取值 `high` / `low`（默认 high）
    - memSymbol：指向目标 kMemory 的 symbol

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
reg ${memSymbol.isSigned ? "signed" : ""} [${memSymbol.width}-1:0] ${symbol};
always @(${clkPolarity} ${clk.symbol} or ${rstPolarity == "high" ? "posedge" : "negedge"} ${rst.symbol}) begin
    // 复位作用于读寄存器，rst_active 时的复位值由 elaboration 显式提供
    if (en_active) begin
        ${symbol} <= ${memSymbol}[${addr.symbol}];
    end
end
assign ${data.symbol} = ${symbol};
```

### 片上存储器写端口 kMemoryWritePort

写端口不提供复位语义；复位行为由上层逻辑显式控制 `en/data`，不自动改写 memory。

- operands：
    - clk：写时钟信号
    - addr：写地址信号
    - en：写使能信号
    - data：写数据输入信号
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol。该 symbol 必须在当前 Graph 内解析到一个 kMemory Operation。
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    if (en_active) begin
        ${memSymbol}[${addr.symbol}] <= ${data.symbol};
    end
end
```

### 片上存储器带掩码写端口 kMemoryMaskWritePort

写端口不提供复位语义；复位行为由上层逻辑显式控制 `en/data/mask`，不自动改写 memory。

- operands：
    - clk：写时钟信号
    - addr：写地址信号
    - en：写使能信号
    - data：写数据输入信号
    - mask: 逐位写掩码信号，位宽必须与 `memSymbol.width` 一致
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol。该 symbol 必须在当前 Graph 内解析到一个 kMemory Operation，掩码逻辑按照该 Operation 的位宽展开。
    - clkPolarity：string 类型，取值 posedge / negedge，指明时钟信号的触发沿
    - enLevel：string 类型，取值 `high` / `low`，指明使能信号的有效极性（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    if (en_active) begin
        if (${mask.symbol} == {${memSymbol.width}{1'b1}}) begin
            ${memSymbol}[${addr.symbol}] <= ${data.symbol};
        end else begin
            for (int i = 0; i < ${memSymbol.width }; i = i + 1) begin
                if (${mask.symbol}[i]) begin
                    ${memSymbol}[${addr.symbol}][i] <= ${data.symbol}[i];
                end
            end
        end
    end
end
```

## 层次结构操作




### 模块实例化 kInstance

kInstance 用于实例化完整定义的模块（Graph），通过 moduleName 关联。

GRH 中的图都是进行参数特化后的，因此 kInstance 不需要参数化支持。

- operands：可变数量的输入信号，m 个 + inout 驱动/使能信号，q 个
    - in0，in1，... in_m-1：模块输入信号
    - inoutOut0，inoutOut1，... inoutOut_q-1：inout 驱动值
    - inoutOe0，inoutOe1，... inoutOe_q-1：inout 使能
- results：可变数量的输出信号，n 个 + inout 读值，q 个
    - out0，out1，... out_n-1：模块输出信号
    - inoutIn0，inoutIn1，... inoutIn_q-1：inout 读值
- attributes：
    - moduleName（string）：被实例化模块的名称，在 netlist 中必须能通过 moduleName 找到一个 graph
    - inputPortName（vector<string>，长度 m）：每个输入信号对应的模块端口名
    - outputPortName（vector<string>，长度 n）：每个输出信号对应的模块端口名
    - inoutPortName（vector<string>，长度 q）：每个 inout 信号对应的模块端口名
    - instanceName（string）：实例名称

生成语义：
```
${moduleName} ${instanceName} (
    .${inputPortName[0]}(${in0.symbol}),
    ...
    .${outputPortName[0]}(${out0.symbol}),
    ...
    .${inoutPortName[0]}(${inoutWire0}),
    ...
);

assign ${inoutWire0} = ${inoutOe0.symbol} ? ${inoutOut0.symbol} : {${width}{1'bz}};
assign ${inoutIn0.symbol} = ${inoutWire0};
```

### 黑盒实例化 kBlackbox

kBlackbox 用于实例化未定义的黑盒模块，支持参数化，生成实例化语法，由仿真综合工具关联实际仿真模型或工艺原语。

- operands：
    - m 个输入信号：in0，in1，... in_m-1
    - q 个 inout 驱动/使能信号：inoutOut0..inoutOut_q-1 / inoutOe0..inoutOe_q-1
- results：
    - n 个输出信号：out0，out1，... out_n-1
    - q 个 inout 读值：inoutIn0..inoutIn_q-1
- attributes：
    - moduleName（string）：被实例化模块的名称
    - inputPortName（vector<string>，长度 m）：每个输入信号对应的模块端口名
    - outputPortName（vector<string>，长度 n）：每个输出信号对应的模块端口名
    - inoutPortName（vector<string>，长度 q）：每个 inout 信号对应的模块端口名
    - parameterNames（vector<string>，长度 p）：每个参数化信号对应的参数名
    - parameterValues（vector<string>，长度 p）：每个参数化信号对应的参数值
    - instanceName（string）：实例名称

生成语义：
```
${moduleName} #(
    .${parameterNames[0]}(${parameterValues[0]}),
    .${parameterNames[1]}(${parameterValues[1]}),
    ...
    .${parameterNames[p-1]}(${parameterValues[p-1]})
) ${instanceName} (
    .${inputPortName[0]}(${in0.symbol}),
    .${inputPortName[1]}(${in1.symbol}),
    ...
    .${inputPortName[m-1]}(${in_m-1.symbol}),
    .${outputPortName[0]}(${out0.symbol}),
    .${outputPortName[1]}(${out1.symbol}),
    ...
    .${outputPortName[n-1]}(${out_n-1.symbol}),
    .${inoutPortName[0]}(${inoutWire0}),
    ...
);

assign ${inoutWire0} = ${inoutOe0.symbol} ? ${inoutOut0.symbol} : {${width}{1'bz}};
assign ${inoutIn0.symbol} = ${inoutWire0};
```

## 调试支持操作

### 打印调试操作 kDisplay

GRH 只建模包裹在有时钟驱动的过程块中的 display，其他情况在生成 GRH 的时候被丢弃。

- operands:
    - clk: 时钟信号
    - enable：使能信号
    - var0，var1，... 可变数量的参与输出的变量, n 个
- attributes:
    - clkPolarity（string）：取值 posedge / negedge，指明时钟信号的触发沿
    - formatString（string）：输出格式化字符串；语法与 SystemVerilog `$display` 一致，支持 `%b/%d/%h/%0d/%0h/%x/%0t` 等常见占位符，默认十进制宽度规则同 SV
    - displayKind（string）：记录原始系统任务，取值 `display` / `write` / `strobe`；运行时可据此决定是否自动追加换行等语义差异

生成语义：
```
always @(${clkPolarity} ${clk.symbol}) begin
    if (${enable.symbol}) begin
        $display("${formatString}", ${var0.symbol}, ${var1.symbol}, ...);
    end
end
```

### 断言操作 kAssert

GRH 只建模包裹在有时钟驱动的过程块中的 assert，其他情况在生成 GRH 的时候被丢弃。

- operands:
    - clk: 时钟信号
    - condition：断言条件
- attributes:
    - clkPolarity（string）：取值 posedge / negedge，指明时钟信号的触发沿
    - message（string，可选）：断言失败的提示文本，通常来自 `$fatal/$error` 参数或静态字符串
    - severity（string，可选）：记录断言级别，建议取值 fatal/error/warning

生成语义：
```
always @(${clkPolarity} ${clk.symbol}) begin
    if (!${condition.symbol}) begin
        $fatal("Assertion failed at time %0t", $time);
    end
end
```

## DPI 操作

### DPI 导入操作 kDpicImport

GRH 目前只提供对 `import "DPI-C" function void svName (arg_type1 arg1, arg_type2 arg2, ...);` 的建模支持，export、task、context、pure 等特性暂不支持。arg 方向支持 input/output/inout，且返回类型必须为 `void`。

具有一个唯一标识符 symbol，供 kDpicCall 引用。

- operands：无
- results：无
- attributes：
    - argsDirection (vector<string>，n个)：记录每个形参的传递方向，取值为 input / output / inout
    - argsWidth (vector<int64_t>，n个)：记录每个形参的位宽
    - argsName (vector<string>，n个)：记录每个形参的名称

生成语义：
```
import "DPI-C" function void ${symbol} (
    ${argsDirection[0]} logic [${argsWidth[0]}-1:0] ${argsName[0]},
    ${argsDirection[1]} logic [${argsWidth[1]}-1:0] ${argsName[1]},
    ...
    ${argsDirection[n-1]} logic [${argsWidth[n-1]}-1:0] ${argsName[n-1]}
);
```

### DPI 调用操作 kDpicCall

- operands：
    - clk: 时钟信号
    - enable：调用使能信号
    - inArg0，inArg1，... 可变数量的输入参数, m 个
    - inoutArg0，inoutArg1，... 可变数量的 inout 输入参数, q 个
- results:
    - outArg0，outArg1，... 可变数量的输出参数, p 个
    - inoutArg0，inoutArg1，... 可变数量的 inout 输出参数, q 个
- attributes：
    - clkPolarity（string）：取值 posedge / negedge，指明时钟信号的触发沿
    - targetImportSymbol（string）：记录被调用 kDpicImport Operation 的 symbol。前端必须在当前 Netlist 中基于该 symbol 查找到唯一的 kDpicImport，将其 `argsName`、`argsDirection` 等元数据注入生成语义中引用的 `targetImportSymbol.*` 字段。
    - inArgName (vector<string>，m 个)：记录每个输入参数的名称
    - outArgName (vector<string>，p 个)：记录每个输出参数的名称
    - inoutArgName (vector<string>，q 个)：记录每个 inout 参数的名称
    - inoutArgName 与 inout operands/results 索引一一对应，且保持 import 声明中的形参顺序

构图或变换流程在处理 `kDpicCall` 时，需使用 `targetImportSymbol` 字符串到 Netlist 中解析出对应的 `kDpicImport` Operation，并从该 Operation 的 attributes 中读取形参方向、位宽与名称等信息；若解析失败或发现多个候选项，必须立即报错。下文伪代码中的 `importOp` 表示解析得到的 kDpicImport Operation，`importOp.argsName` 等字段均来自该 Operation 的 attributes。

生成语义：
```
logic [${outArg0.width}-1:0] ${outArg0.symbol}_intm;
logic [${outArg1.width}-1:0] ${outArg1.symbol}_intm;
logic [${inoutArg0.width}-1:0] ${inoutArg0.symbol}_intm;
...

always @(${clkPolarity} ${clk.symbol}) begin
    if (${enable.symbol}) begin
        ${inoutArg0.symbol}_intm = ${inoutArg0_in.symbol};
        ${inoutArg1.symbol}_intm = ${inoutArg1_in.symbol};
        ...
        ${targetImportSymbol} (
            ${CommaSeparatedList(
                for (size_t i = 0; i < importOp.argsName.size(); ++i)
                    -> (importOp.argsDirection[i] == "input"
                        ? inArgs[IndexOf(inArgName, importOp.argsName[i])].symbol
                        : importOp.argsDirection[i] == "inout"
                          ? inoutOutArgs[IndexOf(inoutArgName, importOp.argsName[i])].symbol + "_intm"
                        : outArgs[IndexOf(outArgName, importOp.argsName[i])].symbol + "_intm")
            )}
        );
    end
end

assign ${outArg0.symbol} = ${outArg0.symbol}_intm;
assign ${outArg1.symbol} = ${outArg1.symbol}_intm;
assign ${inoutArg0.symbol} = ${inoutArg0.symbol}_intm;
...

```

其中：

- `inArgs = {inArg0, inArg1, …, inArgm-1}` 与 `outArgs = {outArg0, outArg1, …, outArgp-1}`；
- `inoutInArgs = {inoutArg0_in, inoutArg1_in, …}` 为 operands 中的 inout 输入参数；
- `inoutOutArgs = {inoutArg0, inoutArg1, …}` 为 results 中的 inout 输出参数；
- `IndexOf(nameList, formalName)` 返回满足 `nameList[k] == formalName` 的唯一索引 `k`；若不存在或存在多个匹配项，则构图逻辑立即报错；
- `inArgName` / `outArgName` / `inoutArgName` 与对应的实参数组在索引维度一一对应；
- 输出与 inout 实参在 DPI 函数调用处以 `_intm` 后缀的中间变量形式传入，其中 inout 会先用输入值初始化中间变量。

# 编译符号信息

- Operation/Value 可携带可选 `SrcLoc`，记录和源码的对应关系
    - 字段 `file`：字符串，源文件路径
    - 字段 `line` / `column`：起始行列
    - 字段 `endLine` / `endColumn`：结束行列
- `file` 为空或 `line == 0` 时视为无效位置
