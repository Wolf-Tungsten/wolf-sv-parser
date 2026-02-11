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
- 时序：`kLatch`、`kRegister`、`kMemory`、`kMemoryReadPort`、`kMemoryWritePort`
- 层次：`kInstance`、`kBlackbox`
- System call：`kSystemFunction`、`kSystemTask`
- DPI：`kDpicImport`、`kDpicCall`

# 边 - Value

满足静态单赋值SSA特性，只能由一个 Operation 写入，可以被多个 Operation 读取。

每个 Value 会显式生成为 SystemVerilog 的信号或变量声明。Logic 类型使用 wire/reg，
Real/String 类型使用 real/string 变量声明（非 net），因此不能使用 continuous assign。

- 具有一个 `SymbolId` 类型的 symbol 字段，用于识别信号，符号来自 `GraphSymbolTable`，在 Graph 作用域内建议唯一且非空；`Graph::internSymbol()` 负责驻留字符串
- 具有一个 `ValueType` 字段：`Logic/Real/String`
- 对于 `Logic`，具有一个 `int32_t` 类型的 width 字段表示位宽（`width` 必须大于 0）
- 对于 `Logic`，具有一个 bool 类型 signed 标记是否为有符号
- `Logic` 支持 SystemVerilog 四态逻辑（0/1/x/z），Value 与 Operation 的语义均按四态传播；常量允许使用 x/z 字面量
- `Real/String` 为变量类型，`width/isSigned` 不参与语义（发射时忽略）
- Value 数据类型对数组和结构体进行扁平化，对于数组和结构体的读写操作通过 kSlice 和 kConcat 实现，不能破坏SSA特性。扁平化顺序遵循 SystemVerilog 的 packed array 和结构体布局规则：同一层级内自左向右（MSB→LSB）展开，多维数组先按最高维（左侧索引）递增，再在每个元素内部继续按 MSB→LSB 展开。

生成语义（简化）

```
wire ${signed ? "signed" : ""} [${width}-1:0] ${symbol};
real ${symbol};
string ${symbol};
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
### 统一锁存器 kLatch

kLatch 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - updateCond：更新条件，必须为 1 bit；无条件更新时使用常量 `1'b1`
    - nextValue：更新值表达式（reset/enable 的优先级需在此表达式中显式编码）
- result：
    - q：锁存器输出

生成语义：
```
reg ${nextValue.signed ? "signed" : ""} [${nextValue.width}-1:0] ${symbol};
always_latch begin
    if (${updateCond.symbol}) begin
        ${symbol} = ${nextValue.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

示例（异步低有效复位 + 使能）：
- updateCond = (!rst_n) || en
- nextValue = (!rst_n) ? resetValue : d

### 统一寄存器 kRegister

kRegister 的 symbol 是必须定义的，且必须符合 verilog 标识符规范。

- operands：
    - updateCond：更新条件，必须为 1 bit；无条件更新时使用常量 `1'b1`
    - nextValue：更新值表达式（reset/enable 的优先级需在此表达式中显式编码）
    - event0, event1, ...：触发事件信号（Value）
- result：
    - q：寄存器输出
- attributes：
    - eventEdge（vector<string>）：触发事件边沿类型列表，取值 `posedge` / `negedge`
    - `eventEdge` 长度必须等于事件信号数量（operand 总数减 2）
    - `eventOperands` = operands[2..]（按顺序与 `eventEdge` 配对）

生成语义：
```
reg ${nextValue.signed ? "signed" : ""} [${nextValue.width}-1:0] ${symbol};
always @(${CommaSeparatedList(zip(eventEdge, eventOperands, " "))}) begin
    if (${updateCond.symbol}) begin
        ${symbol} <= ${nextValue.symbol};
    end
end
assign ${q.symbol} = ${symbol};
```

示例（异步低有效复位 + 使能）：
- eventOperands = [clk, rst_n]
- eventEdge = ["posedge", "negedge"]
- updateCond = (!rst_n) || en
- nextValue = (!rst_n) ? resetValue : d

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

### 片上存储器读端口 kMemoryReadPort

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

说明：同步读端口使用 `kRegister` 捕获 `kMemoryReadPort` 的输出，
在事件 operands 与 `eventEdge` 中声明时钟/复位事件，并通过 `updateCond/nextValue` 编码 enable/reset 语义。

### 片上存储器写端口 kMemoryWritePort

写端口不提供复位语义；复位行为由上层逻辑显式控制 `updateCond/data`，不自动改写 memory。

- operands：
    - updateCond：写入条件，必须为 1 bit
    - addr：写地址信号
    - data：写数据输入信号
    - mask：逐位写掩码信号，位宽必须与 `memSymbol.width` 一致；无掩码写入时使用常量全 1
    - event0, event1, ...：触发事件信号（Value）
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol。该 symbol 必须在当前 Graph 内解析到一个 kMemory Operation。
    - eventEdge（vector<string>）：触发事件边沿类型列表，取值 `posedge` / `negedge`
    - `eventEdge` 长度必须等于事件信号数量（operand 总数减 4）
    - `eventOperands` = operands[4..]（按顺序与 `eventEdge` 配对）

生成语义：
```
always @(${CommaSeparatedList(zip(eventEdge, eventOperands, " "))}) begin
    if (${updateCond.symbol}) begin
        if (${mask.symbol} == {${memSymbol.width}{1'b1}}) begin
            ${memSymbol}[${addr.symbol}] <= ${data.symbol};
        end else begin
            for (int i = 0; i < ${memSymbol.width}; i = i + 1) begin
                if (${mask.symbol}[i]) begin
                    ${memSymbol}[${addr.symbol}][i] <= ${data.symbol}[i];
                end
            end
        end
    end
end
```

## 层次结构操作


### 层次引用 kXMRRead / kXMRWrite

用于记录层次化引用（XMR）的读写意图。该类 Operation 仅作为中间表示存在，
必须在 emit 前通过 resolve pass 展开为端口/实例连接及最终 `kAssign`。

#### kXMRRead
- operands：无
- results：
    - out：读取到的信号值（单个 result）
- attributes：
    - xmrPath（string）：层次路径（相对当前 Graph 的实例路径 + 目标信号）

#### kXMRWrite
- operands：
    - data：写入值（单个 operand）
- results：无
- attributes：
    - xmrPath（string）：层次路径（相对当前 Graph 的实例路径 + 目标信号）

说明：
- resolve pass 会沿层次路径为中间模块添加端口、更新实例端口连接，
  并在叶子模块生成 `kAssign`，保证单一驱动语义。
- `kXMRRead/kXMRWrite` 在 emit 阶段不应残留；若未解析则视为错误。




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

## System call 操作

### System function 操作 kSystemFunction

用于表达 `$time/$random/$urandom_range/$sformatf` 等系统函数调用（表达式侧）。

- operands：
    - arg0，arg1，... 可变数量的输入参数
- results：
    - 单一返回值（Value）
- attributes：
    - name（string）：系统函数名（去掉 `$` 的规范化名称）
    - hasSideEffects（bool，可选）：标记 `$random/$urandom` 等带副作用函数

已支持的 system function（kSystemFunction）：
- 运行期函数（保留为 kSystemFunction）：
  - `$time/$stime/$realtime`
  - `$random/$urandom/$urandom_range`
  - `$fopen/$ferror`
  - `$sformatf/$psprintf`
  - `$itor/$rtoi/$realtobits/$bitstoreal`
- 可折叠为常量（参数可解析时）：
  - `$bits`（直接生成常量）
  - `$clog2/$size`（参数可解析时折叠为常量，否则保留为 kSystemFunction）
- `$signed/$unsigned` 不生成 kSystemFunction，保持为显式 cast（kAssign）

参数约束：
- `$time/$stime/$realtime`：不接受参数
- `$random/$urandom`：支持 0~1 个参数
- `$urandom_range`：支持 1~2 个参数
- `$fopen`：支持 1~2 个参数（文件名 + 可选模式）
- `$ferror`：仅支持 1 个参数（文件句柄）；不支持输出字符串参数
- `$itor/$rtoi/$realtobits/$bitstoreal`：仅支持 1 个参数
- `$sformatf/$psprintf`：至少 1 个参数（format string + 可变参数）
- `$clog2/$size`：仅支持 1 个参数

生成语义：
```
${result.symbol} = $${name}(${arg0.symbol}, ${arg1.symbol}, ...);
```

### System task 操作 kSystemTask

统一承载 `$display/$fwrite/$fatal/$finish/$dumpvars/...` 等系统任务（语句侧）。

- operands：
    - callCond：触发条件，必须为 1 bit；无条件触发时使用常量 `1'b1`
    - arg0，arg1，... 可变数量的任务参数（包含 format string / file handle / exit code）
    - event0，event1，...：触发事件信号（Value）
- attributes：
    - name（string）：系统任务名（去掉 `$` 的规范化名称）
    - eventEdge（vector<string>，可选）：触发事件边沿类型列表，取值 `posedge` / `negedge`
    - procKind（string）：过程块类型（initial/final/always_*）
    - hasTiming（bool）：是否显式 timing control

已支持的 system task（kSystemTask）：
- `$display/$write/$strobe`
- `$fwrite/$fdisplay/$fclose/$fflush`
- `$info/$warning/$error/$fatal`
- `$finish/$stop`
- `$dumpfile/$dumpvars`（仅支持全量 dump；其它参数给 warning 并忽略）

不作为 kSystemTask 处理的 system task：
- `$readmemh/$readmemb`：记录为 `kMemory` 的初始化属性并在 emit 阶段生成 `initial $readmem*`
- `$sdf_annotate/$monitor/$monitoron/$monitoroff`：warning 并忽略（不生成 kSystemTask）

约束：
- `eventEdge` 长度必须等于 event 操作数数量
- `eventOperands` = operands[1 + args ..]

说明：
- 当 `eventEdge` 为空时，emit 依据 `procKind` 生成 `initial/final/always_*` 过程块

生成语义：
```
always @(${CommaSeparatedList(zip(eventEdge, eventOperands, " "))}) begin
    if (${callCond.symbol}) begin
        $${name}(${arg0.symbol}, ${arg1.symbol}, ...);
    end
end
```

## DPI 操作

### DPI 导入操作 kDpicImport

GRH 目前只提供对 `import "DPI-C" function svName (arg_type1 arg1, arg_type2 arg2, ...);` 的建模支持，export、task、context、pure 等特性暂不支持。arg 方向仅支持 input/output；返回类型可选（void 或有返回值）。

具有一个唯一标识符 symbol，供 kDpicCall 引用。

- operands：无
- results：无
- attributes：
    - argsDirection (vector<string>，n个)：记录每个形参的传递方向，取值为 input / output
    - argsWidth (vector<int64_t>，n个)：记录每个形参的位宽
    - argsName (vector<string>，n个)：记录每个形参的名称
    - argsSigned (vector<bool>，n个)：记录每个形参是否为有符号
    - argsType (vector<string>，n个)：记录每个形参的数据类型，取值为 "logic" / "string"（缺省视为 "logic"）
    - hasReturn (bool)：是否有返回值；false 表示 void
    - returnWidth (int64_t)：返回值位宽（hasReturn 为 true 时有效）
    - returnSigned (bool)：返回值是否有符号（hasReturn 为 true 时有效）
    - returnType (string)：返回值类型（hasReturn 为 true 时有效）；取值为 "logic" / "string"（缺省视为 "logic"）

约束：
- `hasReturn == true` 时，`returnWidth > 0` 且 `returnSigned` 有效
- `argsSigned/argsType` 与 `argsName/argsDirection/argsWidth` 长度一致

生成语义：
```
import "DPI-C" function ${hasReturn ? (returnType == "string" ? "string" :
    ("logic " + (returnSigned ? "signed " : "") + "[" + (returnWidth-1) + ":0]")) : "void"} ${symbol} (
    ${argsDirection[i]} ${argsType[i] == "string" ? "string" :
        ("logic " + (argsSigned[i] ? "signed " : "") + "[" + (argsWidth[i]-1) + ":0]")} ${argsName[i]},
    ...
);
```

### DPI 调用操作 kDpicCall

- operands：
    - updateCond：调用触发条件，必须为 1 bit；无条件触发时使用常量 `1'b1`
    - inArg0，inArg1，... 可变数量的输入参数, m 个
    - event0, event1, ...：触发事件信号（Value）
- results:
    - retVal（可选）：当 `hasReturn` 为 true 时，第一个 result 作为返回值
    - outArg0，outArg1，... 可变数量的输出参数, p 个
- attributes：
    - eventEdge（vector<string>）：触发事件边沿类型列表，取值 `posedge` / `negedge`
    - targetImportSymbol（string）：记录被调用 kDpicImport Operation 的 symbol
    - inArgName (vector<string>，m 个)：记录每个输入参数的名称
    - outArgName (vector<string>，p 个)：记录每个输出参数的名称
    - hasReturn (bool)：是否有返回值

约束：
- `eventEdge` 长度必须等于事件信号数量（operand 总数减 1 - m）
- `eventOperands` = operands[1 + m ..]（按顺序与 `eventEdge` 配对）
- `targetImportSymbol` 必须在当前 Netlist 中解析到唯一的 kDpicImport；
  前端需从该 Operation 注入 `argsName/argsDirection` 等元数据
- `hasReturn == true` 时，`results[0]` 为返回值，`results[1..]` 为输出参数；
  `hasReturn == false` 时，results 仅包含输出参数

构图或变换流程在处理 `kDpicCall` 时，需使用 `targetImportSymbol` 字符串到 Netlist 中解析出对应的 `kDpicImport` Operation，并从该 Operation 的 attributes 中读取形参方向、位宽与名称等信息；若解析失败或发现多个候选项，必须立即报错。下文伪代码中的 `importOp` 表示解析得到的 kDpicImport Operation，`importOp.argsName` 等字段均来自该 Operation 的 attributes。

生成语义：
```
logic ${retVal.signed ? "signed" : ""} [${retVal.width}-1:0] ${retVal.symbol}_intm; // hasReturn 为 true 时生成
logic [${outArg0.width}-1:0] ${outArg0.symbol}_intm;
logic [${outArg1.width}-1:0] ${outArg1.symbol}_intm;
...

always @(${CommaSeparatedList(zip(eventEdge, eventOperands, " "))}) begin
    if (${updateCond.symbol}) begin
        ${hasReturn ? (retVal.symbol + "_intm = ") : ""}${targetImportSymbol} (
          ${CommaSeparatedList(
              for (size_t i = 0; i < importOp.argsName.size(); ++i)
                  -> (importOp.argsDirection[i] == "input"
                      ? inArgs[IndexOf(inArgName, importOp.argsName[i])].symbol
                      : outArgs[IndexOf(outArgName, importOp.argsName[i])].symbol + "_intm")
          )}
        );
    end
end

${hasReturn ? ("assign " + retVal.symbol + " = " + retVal.symbol + "_intm;") : ""}
assign ${outArg0.symbol} = ${outArg0.symbol}_intm;
assign ${outArg1.symbol} = ${outArg1.symbol}_intm;

```

其中：

- `inArgs = {inArg0, inArg1, …, inArgm-1}` 与 `outArgs = {outArg0, outArg1, …, outArgp-1}`；
- `IndexOf(nameList, formalName)` 返回满足 `nameList[k] == formalName` 的唯一索引 `k`；若不存在或存在多个匹配项，则构图逻辑立即报错；
- `inArgName` / `outArgName` 与对应的实参数组在索引维度一一对应；
- 输出实参在 DPI 函数调用处以 `_intm` 后缀的中间变量形式传入。
- 当 `hasReturn` 为 true 时，`results[0]` 视为返回值，`results[1..]` 对应输出参数；
  返回值通过 `_intm` 中间变量写回。

# 编译符号信息

- Operation/Value 可携带可选 `SrcLoc`，记录和源码的对应关系
    - 字段 `file`：字符串，源文件路径
    - 字段 `line` / `column`：起始行列
    - 字段 `endLine` / `endColumn`：结束行列
- `file` 为空或 `line == 0` 时视为无效位置
