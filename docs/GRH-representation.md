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

# 顶点 - Operation

- Operation 具有类型：记录在枚举类型的 type 字段，Operation 的类型是有限的、预定义的
- Operation 可被符号索引：具有一个字符串类型的 symbol 字段，作为 Operation 添加可索引的标签，在 Graph 作用域内保持唯一，symbol 应尽可能沿用输入RTL的命名，提高可读性
- Operation 接受操作数作为输入：具有一个 operands 数组，包含 Value 指针
- Operation 输出结果：具有一个 results 数组，包含作为结果的 Value 指针
- Operation 的属性：具有一个 attributes 字典记录元数据
    - attributes key：以 string 类型作为 key
    - attributes value 允许的类型
        - 基本类型（basic_type）：`bool`、`int64_t`、`double`、`std::string`
        - 包含基本类型的数组：`vector<basic_type>`


## Operation 类型概览

- 常量：`kConstant`
- 组合逻辑：`kAdd`、`kSub`、`kMul`、`kDiv`、`kMod`、`kEq`、`kNe`、`kLt`、`kLe`、`kGt`、`kGe`、`kAnd`、`kOr`、`kXor`、`kXnor`、`kNot`、`kLogicAnd`、`kLogicOr`、`kLogicNot`、`kReduceAnd`、`kReduceOr`、`kReduceXor`、`kReduceNor`、`kReduceNand`、`kReduceXnor`、`kShl`、`kLShr`、`kAShr`、`kMux`
- 连线：`kAssign`、`kConcat`、`kReplicate`、`kSliceStatic`、`kSliceDynamic`、`kSliceArray`
- 时序：`kLatch`、`kLatchArst`、`kRegister`、`kRegisterEn`、`kRegisterRst`、`kRegisterEnRst`、`kRegisterArst`、`kRegisterEnArst`、`kMemory`、`kMemoryAsyncReadPort`、`kMemorySyncReadPort`、`kMemorySyncReadPortRst`、`kMemorySyncReadPortArst`、`kMemoryWritePort`、`kMemoryWritePortRst`、`kMemoryWritePortArst`、`kMemoryMaskWritePort`、`kMemoryMaskWritePortRst`、`kMemoryMaskWritePortArst`
- 层次：`kInstance`、`kBlackbox`
- 调试：`kDisplay`、`kAssert`
- DPI：`kDpicImport`、`kDpicCall`

# 边 - Value

满足静态单赋值SSA特性，只能由一个 Operation 写入，可以被多个 Operation 读取。

每个 Value 最终都会显式生成为 SystemVerilog 中的 wire 声明，以支持 SSA 特性。

- 具有一个字符串类型的 symbol 字段，用于识别信号，要求在 Graph 作用域内唯一且非空，符合verilog 标识符规范，symbol 应尽可能沿用输入RTL的命名，提高可读性
- 具有一个 `int64_t` 类型的 width 字段，表示 Value 的位宽，`width` 必须大于 0
- 具有一个 bool 类型 signed 标记是否为有符号
- Value 数据类型对数组和结构体进行扁平化，对于数组和结构体的读写操作通过 kSlice 和 kConcat 实现，不能破坏SSA特性。扁平化顺序遵循 SystemVerilog 的 packed array 和结构体布局规则：同一层级内自左向右（MSB→LSB）展开，多维数组先按最高维（左侧索引）递增，再在每个元素内部继续按 MSB→LSB 展开。

生成语义

```
wire ${signed ? "signed" : ""} [${width}-1:0] ${symbol};
```

记录是否作为模块端口

- 具有一个标记是否为模块输入的布尔字段 isInput
- 具有一个标记是否为模块输出的布尔字段 isOutput
- 以上二者不能同时为真
- 当 `isInput == true` 时，该 Value 必须存在于所属 Graph 的 `inputPorts` 字典中，键名与端口名一致；同理，`isOutput == true` 的 Value 必须存在于 `outputPorts` 字典中。未出现在端口字典中的 Value 必须同时满足 `isInput == false` 且 `isOutput == false`。

辅助字段

- 具有一个 graph 指针，指向所属的图
- 具有一个 defineOp 指针，指向写入 Op，若 Value 是模块的输入参数，则为空指针
- 具有一个 userOps 数组，元素为 `<Operation*, size_t operandIndex>` 二元组，记录该 Value 在各 Operation 中作为第几个操作数被引用；同一 Operation 多次使用同一个 Value 时会存储多个条目
- Graph 创建 Value 时即拥有其生命周期，禁止在多个 Graph 之间共享 Value。`defineOp` 和 `userOps` 都只能引用同一个 Graph 的 Operation。

# 图 - Graph

建模一个参数化后的 module：

- 具有一个 moduleName 字段标识模块名称，moduleName 符合 verilog 标识符规范，在网表中唯一
- 具有一个 inputPorts 字段，类型为 map<std::string，Value*> 的字典，记录所有输入端口，导出时按端口名的字典序遍历
- 具有一个 outputPorts 字段，类型为 map<std::string，Value*> 的字典，记录所有输出端口，导出时按端口名的字典序遍历
- 不支持 inout 类型端口
- 具有一个 isTopModule bool 字段，标识是否为顶层模块

生成语义

```
module ${moduleName} (
    ${CommaSeparatedList(
        for (const auto& [name, value] : inputPorts)
            -> "input "  + (value->signed ? "signed " : "")
               + "[" + std::to_string(value->width - 1) + ":0] " + name,
        for (const auto& [name, value] : outputPorts)
            -> "output " + (value->signed ? "signed " : "")
               + "[" + std::to_string(value->width - 1) + ":0] " + name
    )}
);
    由 Graph 内部的 Operation 和 Value 生成的语句
endmodule
```

其中 `CommaSeparatedList` 先按照端口名的字典序枚举全部输入端口，再按照端口名字典序枚举全部输出端口；端口顺序不会保留原始 SystemVerilog 声明顺序。

Graph 管理 Operation 和 Value 的生命周期：

- 具有一个 values 数组，保存所有 value 的指针。Graph 对 values 拥有所有权，在 Graph 析构时销毁全部 values，并维持插入顺序用于遍历
- 具有一个 ops 数组，保存所有 operation 的指针。Graph 对 ops 拥有所有权，在 Graph 析构时销毁全部 operation，ops 按插入顺序保存，不保证拓扑顺序

# 网表 - Netlist

- 具有一个可以按照 moduleName 索引全部 Graph 的容器，要求 moduleName 在网表中唯一
- 通过 Graph 的 isTopModule 字段标记顶层模块，至少存在一个顶层 Graph，且顶层 Graph 不允许被其他 Graph 的 instance 引用
- instance Operation 的 attributes 中记录被例化模块的 moduleName，运行时通过该索引解析被例化 Graph；禁止跨网表引用
- 允许存在未被引用的 Graph（如库模块），但综合或导出流程默认仅从顶层 Graph 开始遍历可达子图
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

### 二元操作符

包含 `kAdd`(+)、`kSub`(-)、`kMul`(*)、`kDiv`(/)、`kMod`(%)、`kEq`(==)、`kNe`(!=)、`kLt`(<)、`kLe`(<=)、`kGt`(>)、`kGe`(>=)、`kAnd`(&)、`kOr`(|)、`kXor`(^)、`kXnor`(~^)、`kLogicAnd`(&&)、`kLogicOr`(||)、、`kShl`(<<)、`kLShr`(>>)、`kAShr`(>>>)。

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
        for (Value* operand : operands)
            -> operand->symbol
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
    // rst_active 是否触发输出复位由 elaboration 提供的 en/data 决定；en_active 可能已包含 rst_active
    if (en_active) begin
        ${symbol} <= ${memSymbol}[${addr.symbol}];
    end
end
assign ${data.symbol} = ${symbol};
```

### 片上存储器同步读端口（异步复位） kMemorySyncReadPortArst

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
    // rst_active 是否触发输出复位由 elaboration 提供的 en/data 决定；en_active 可能已包含 rst_active
    if (en_active) begin
        ${symbol} <= ${memSymbol}[${addr.symbol}];
    end
end
assign ${data.symbol} = ${symbol};
```

### 片上存储器写端口 kMemoryWritePort

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

### 片上存储器写端口（同步复位） kMemoryWritePortRst

- operands：
    - clk：写时钟信号
    - rst：复位信号
    - addr：写地址信号
    - en：写使能信号
    - data：写数据输入信号
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol
    - clkPolarity：string 类型，取值 posedge / negedge
    - rstPolarity：string 类型，取值 `high` / `low`
    - enLevel：string 类型，取值 `high` / `low`（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    // rst_active 是否触发写入由 elaboration 生成的 en/data 决定；en_active 可直接包含 rst 相关条件
    if (en_active) begin
        ${memSymbol}[${addr.symbol}] <= ${data.symbol};
    end
end
```

### 片上存储器写端口（异步复位） kMemoryWritePortArst

- operands：
    - clk：写时钟信号
    - rst：复位信号
    - addr：写地址信号
    - en：写使能信号
    - data：写数据输入信号
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol
    - clkPolarity：string 类型，取值 posedge / negedge
    - rstPolarity：string 类型，取值 `high` / `low`，决定敏感沿
    - enLevel：string 类型，取值 `high` / `low`（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
always @(${clkPolarity} ${clk.symbol} or ${rstPolarity == "high" ? "posedge" : "negedge"} ${rst.symbol}) begin
    if (en_active) begin
        ${memSymbol}[${addr.symbol}] <= ${data.symbol};
    end
end
```

### 片上存储器带掩码写端口 kMemoryMaskWritePort

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

### 片上存储器带掩码写端口（同步复位） kMemoryMaskWritePortRst

- operands：
    - clk：写时钟信号
    - rst：复位信号
    - addr：写地址信号
    - en：写使能信号
    - data：写数据输入信号
    - mask: 逐位写掩码信号
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol
    - clkPolarity：string 类型，取值 posedge / negedge
    - rstPolarity：string 类型，取值 `high` / `low`
    - enLevel：string 类型，取值 `high` / `low`（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
always @(${clkPolarity} ${clk.symbol}) begin
    // rst_active 是否触发写入由 elaboration 生成的 en/data/mask 决定；en_active 可直接包含 rst 相关条件
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

### 片上存储器带掩码写端口（异步复位） kMemoryMaskWritePortArst

- operands：
    - clk：写时钟信号
    - rst：复位信号
    - addr：写地址信号
    - en：写使能信号
    - data：写数据输入信号
    - mask: 逐位写掩码信号
- results：无
- attributes：
    - memSymbol：指向目标 kMemory 的 symbol
    - clkPolarity：string 类型，取值 posedge / negedge
    - rstPolarity：string 类型，取值 `high` / `low`，决定敏感沿
    - enLevel：string 类型，取值 `high` / `low`（默认 high）

生成语义：
```
wire en_active = (enLevel == "high") ? ${en.symbol} : !${en.symbol};
wire rst_active = (rstPolarity == "high") ? ${rst.symbol} : !${rst.symbol};
always @(${clkPolarity} ${clk.symbol} or ${rstPolarity == "high" ? "posedge" : "negedge"} ${rst.symbol}) begin
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

- operands：可变数量的输入信号，m 个
    - in0，in1，... in_m-1：模块输入信号
- results：可变数量的输出信号，n 个
    - out0，out1，... out_n-1：模块输出信号
- attributes：
    - moduleName（string）：被实例化模块的名称，在 netlist 中必须能通过 moduleName 找到一个 graph
    - inputPortName（vector<string>，长度 m）：每个输入信号对应的模块端口名
    - outputPortName（vector<string>，长度 n）：每个输出信号对应的模块端口名
    - instanceName（string）：实例名称

生成语义：
```
${moduleName} ${instanceName} (
    .${inputPortName[0]}(${in0.symbol}),
    .${inputPortName[1]}(${in1.symbol}),
    ...
    .${inputPortName[m-1]}(${in_m-1.symbol}),
    .${outputPortName[0]}(${out0.symbol}),
    .${outputPortName[1]}(${out1.symbol}),
    ...
    .${outputPortName[n-1]}(${out_n-1.symbol})
);
```

### 黑盒实例化 kBlackbox

kBlackbox 用于实例化未定义的黑盒模块，支持参数化，生成实例化语法，由仿真综合工具关联实际仿真模型或工艺原语。

- operands：
    - m 个输入信号：in0，in1，... in_m-1
- results：
    - n 个输出信号：out0，out1，... out_n-1
- attributes：
    - moduleName（string）：被实例化模块的名称
    - inputPortName（vector<string>，长度 m）：每个输入信号对应的模块端口名
    - outputPortName（vector<string>，长度 n）：每个输出信号对应的模块端口名
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
    .${outputPortName[n-1]}(${out_n-1.symbol})
);
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

GRH 目前只提供对 `import "DPI-C" function void svName (arg_type1 arg1, arg_type2 arg2, ...);` 的建模支持，export、task、context、pure 等特性暂不支持。arg 方向只支持 input 或 output，inout 不支持，且返回类型必须为 `void`。

具有一个唯一标识符 symbol，供 kDpicCall 引用。

- operands：无
- results：无
- attributes：
    - argsDirection (vector<string>，n个)：记录每个形参的传递方向，取值为 input / output，不允许 inout
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
- results:
    - outArg0，outArg1，... 可变数量的输出参数, p 个
- attributes：
    - clkPolarity（string）：取值 posedge / negedge，指明时钟信号的触发沿
    - targetImportSymbol（string）：记录被调用 kDpicImport Operation 的 symbol。前端必须在当前 Netlist 中基于该 symbol 查找到唯一的 kDpicImport，将其 `argsName`、`argsDirection` 等元数据注入生成语义中引用的 `targetImportSymbol.*` 字段。
    - inArgName (vector<string>，m 个)：记录每个输入参数的名称
    - outArgName (vector<string>，p 个)：记录每个输出参数的名称

构图或变换流程在处理 `kDpicCall` 时，需使用 `targetImportSymbol` 字符串到 Netlist 中解析出对应的 `kDpicImport` Operation，并从该 Operation 的 attributes 中读取形参方向、位宽与名称等信息；若解析失败或发现多个候选项，必须立即报错。下文伪代码中的 `importOp` 表示解析得到的 kDpicImport Operation，`importOp.argsName` 等字段均来自该 Operation 的 attributes。

生成语义：
```
logic [${outArg0.width}-1:0] ${outArg0.symbol}_intm;
logic [${outArg1.width}-1:0] ${outArg1.symbol}_intm;
...

always @(${clkPolarity} ${clk.symbol}) begin
    if (${enable.symbol}) begin
        ${targetImportSymbol} (
            ${CommaSeparatedList(
                for (size_t i = 0; i < importOp.argsName.size(); ++i)
                    -> (importOp.argsDirection[i] == "input"
                        ? inArgs[IndexOf(inArgName, importOp.argsName[i])].symbol
                        : outArgs[IndexOf(outArgName, importOp.argsName[i])].symbol + "_intm")
            )}
        );
    end
end

assign ${outArg0.symbol} = ${outArg0.symbol}_intm;
assign ${outArg1.symbol} = ${outArg1.symbol}_intm;
...

```

其中：

- `inArgs = {inArg0, inArg1, …, inArgm-1}` 与 `outArgs = {outArg0, outArg1, …, outArgp-1}` ；
- `IndexOf(nameList, formalName)` 返回满足 `nameList[k] == formalName` 的唯一索引 `k`；若不存在或存在多个匹配项，则构图逻辑立即报错；
- `inArgName` / `outArgName` 与 `inArgs` / `outArgs` 在索引维度一一对应，因此 `inArgs[IndexOf(inArgName, formalName)].symbol` 表示实参中名称与形参 `formalName` 对应的输入信号，`outArgs[IndexOf(outArgName, formalName)].symbol` 表示对应的输出信号；
- 输出实参在 DPI 函数调用处以 `_intm` 后缀的中间变量形式传入，以匹配前面声明的中间寄存器。

# 编译符号信息

- 所有 Operation/Value/Graph 都携带一组 `srcLoc*` 成员变量，记录和源码的对应关系
    - 字段`sourceLocfile`：字符串，源文件路径
    - 字段`srcLocLine`：int64_t，行号
    - 字段`srcLocColumn`：int64_t，列号
    - 字段`srcLocHierPath`：字符串，层次路径，使用 `.` 分隔层次（例如 `top.u_sub.u_leaf`）
- 上述字段为空或负值时表示无效位置
