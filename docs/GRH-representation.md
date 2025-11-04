# Graph RTL Hierarchy（GRH）Representation

借鉴 MLIR SSA 形式的描述，融合 RTL 设计特点定制

# 自底向上的层次描述

- 顶点（Operation）和超边（Value）
    - 顶点：用 Operation 建模组合逻辑操作、寄存器、模块实例化
    - 边：用 Value 建模操作、寄存器、模块实例化之间的连接
- 图（Graph）
    - 作为 Operation 和 Value 的容器，包含一组关联的顶点和边
    - 每个图建模一个 Verilog Module
- 网表（Netlist）
    - 是图的集合
    - 存在一个或多个图作为顶层模块

# 顶点 - Operation

- 具有一个枚举类型的 type 字段，表示 Operation 的类型，Operation 的类型是有限的
- 具有一个字符串类型的 symbol 字段，为 Operation 添加可索引的标签
- 具有一个 operands 数组，包含对 Value 的指针，记录 Operation 的操作数
- 具有一个 results 数组，包含对 Value 的指针，记录 Operation 的结果
- 具有一个 attributes 字典，key 为字符串类型，value 为 std::any，记录 Operation 的元数据。attributes 的 value 仅允许存储 `bool`、`int64_t`、`uint64_t`、`double`、`std::string`、`std::vector<std::any>`、`std::map<std::string, std::any>` 等 JSON 兼容类型，序列化时按照 JSON 语义写出，遇到不受支持的类型时抛错
- 所有 Operation/Value/Graph 都可以携带一个可选的 `sourceLoc` 属性，用 `std::map<std::string, std::any>` 结构表示；推荐字段包括 `file`（字符串，源文件路径）、`line`（uint64_t，行号）、`column`（uint64_t，列号）、`hierPath`（字符串，层次路径）。缺失字段表示对应信息未知

## Operation 类型与使用约束

- `type` 字段对应 `enum class OperationKind`，当前枚举值依次为：
  - 常量：`kConstant`
  - 组合算术/逻辑：`kAdd`、`kSub`、`kMul`、`kDiv`、`kMod`、`kEq`、`kNe`、`kLt`、`kLe`、`kGt`、`kGe`、`kAnd`、`kOr`、`kXor`、`kXnor`、`kNot`、`kLogicAnd`、`kLogicOr`、`kLogicNot`、`kReduceAnd`、`kReduceOr`、`kReduceXor`、`kReduceNor`、`kReduceNand`、`kReduceXnor`、`kShl`、`kLShr`、`kAShr`、`kMux`、`kSlice`、`kConcat`、`kReplicate`
  - 时序/层次/调试：`kRegister`、`kMemory`、`kMemoryReadPort`、`kMemoryWritePort`、`kInstance`、`kDisplay`、`kAssert`、`kDpicImport`、`kDpicCall`
  若需要扩展新的 Operation，需要同步扩展该枚举并更新本文件。
- `addOperand(Value*)` 会把当前 Operation 注册到 Value 的 `userOps` 中，`addResult(Value*)` 则会把 Value 的 `defineOp` 设为当前 Operation。前端/解析器应优先使用 Graph 提供的构造接口，以自动维护 def-use 关系。
- `symbol` 在 Graph 作用域内建议唯一，方便调试和序列化（系统不会强制检查，约定由前端保证）。

## Operation 的粗粒度分类描述

- 常量定义操作
    - 没有 operand Value，只有一个输出 result Value
    - 常量 symbol 应尽可能可读，尽可能沿用用户输入代码的命名
    - 常量位宽以 uint64_t 类型名为 width 的 attribute 标识
    - 常量值的记录方法
        - 使用 bool signed attribute 标识是否为有符号常量
        - 使用 bool wide attribute 标识是否为位宽大于 64 bit 的值
        - 当常量值位宽小于等于64位时，使用 int64_t 或 uint64_t 的 value 属性直接保存
        - 当常量值位宽大于64位时，使用 vector<int64_t> 或 vector<uint64_t> 的 wideValue 属性存储常量值，小端序形式，索引 i 保存 [i*64 +: 64] 位

- 组合逻辑操作
    - 支持 SystemVerilog 的算术操作符、相等操作符、逻辑操作符、按位操作符、缩减操作符、移位操作符、关系操作符、条件操作符、拼接和复制操作符
    - 所有操作符的操作数都用 operand 传入
    - 条件操作使用 2选1 MUX operation（`kMux`）建模，具有三个 operand，第一个为判断条件，第二个为条件真时结果，第三个为条件假时结果
    - 相等/关系操作分别映射到 `kEq`、`kNe`、`kLt`、`kLe`、`kGt`、`kGe`
    - 逻辑操作（`&&`/`||`/`!`）使用 `kLogicAnd`、`kLogicOr`、`kLogicNot`
    - 按位操作包含 `kAnd`、`kOr`、`kXor`、`kXnor`、`kNot`
    - 缩减操作使用 `kReduce*` 对应的 OperationKind，并只允许一个 operand
    - 拼接操作的 operand 数不固定，结果为 operand 从低索引到高索引、从右至左拼接而成
    - 复制操作使用 `kReplicate`，被复制的变量以 operand 方式传入，副本数量用 uint64_t `rep` attribute 保存

- 位截取操作
    - 所有位截取均复用 `kSlice` Operation，通过字符串属性 `sliceKind` 区分模式，取值 `static`（静态）、`dynamic`（动态）、`array`（数组）。
    - `static`：一个 operand `o`，一个输出 `r`，属性 `start`/`end`（`uint64_t`）均包含端点，语义 `r = o[end : start]`。
    - `dynamic`：两个 operanda（`a` 为被截取输入，`b` 为截取起点），一个输出 `r`，属性 `width`（`uint64_t`），语义 `r = a[b +: width]`。
    - `array`：两个 operanda（`a` 为被截取输入，`i` 为数组索引），一个输出 `r`，属性 `width`（`uint64_t`），语义 `r = a[i * width +: width]`。
    - 为了便于分析，GRH 的 Value 不保留数组或结构体拓扑信息；数组（含多维）访存通过级联 `sliceKind=array` 完成，结构体访问使用 `sliceKind=static`。

- 时序逻辑部件操作
    - 寄存器: OperationKind::kRegister
        - 当 `resetCategory` 属性取 `sync` 时，建模同步复位寄存器
            - 操作数 1 为时钟信号
            - 操作数 2 为 d 信号
            - 结果为 q 信号
            - 字符串类型的 clkType 属性可取值 posedge、negedge、edge
        - 当 `resetCategory` 属性取 `async` 时，建模异步复位寄存器
            - 操作数 1 为时钟信号
            - 操作数 2 为复位信号
            - 操作数 3 为 d 信号
            - 结果为 q 信号
            - 字符串类型的 clkType 属性可取值 posedge、negedge、edge
            - 字符串类型的 rstType 属性可取值 posedge、negedge、edge
        - 如果未设置 `resetCategory`，默认视为同步复位
    - 片上 mem: OperationKind::kMemory、OperationKind::kMemoryReadPort、OperationKind::kMemoryWritePort
        - kMemory 操作建模存储阵列
            - 没有操作数，也没有结果
            - uint64_t width 属性记录每行bit数位宽
            - uint64_t row 属性记录总行数
            - 设置一个全局唯一的 symbol 用于 kMemoryReadPort 和 kMemoryWritePort 索引，该 symbol 应尽可能人类可读，便于后续调试
        - kMemoryReadPort
            - 操作数 1 为读地址信号
            - 结果为读数据信号
            - 字符串类型的 memSymbol 属性指向对应的 kMemory 组件
            - kMemoryReadPort 建模的读取为异步读取。如果需要同步读取，通过与设置了 `resetCategory=sync` 的 kRegister 联合实现
        - kMemoryWritePort
            - 操作数 1 为写时钟信号
            - 操作数 2 为写地址信号
            - 操作数 3 为写数据信号
            - 字符串类型的 memSymbol 属性指向对应的 kMemory 组件
            - kMemoryWritePort 建模为同步写入
            - 字符串类型的 clkType 属性可取值 posedge、negedge、edge
        - 一个 kMemory 可以关联多个读写口，以支持跨时钟域读写等场景

- 层次结构操作：kInstance
    - 字符串类型的 moduleName 属性，用于识别被实例化的模块（也就是 Graph）
    - 可变数量的操作数，表示被实例化模块的输入
    - 一个 vector<string> inputPortName，顺序和数量与操作数一致，表示每个操作数和模块输入的关系映射
    - 可变数量的结果result，表示被实例化模块的输出
    - 一个 vector<string> outputPortName，顺序和数量与操作数一致，表示每个结果和模块输出的关系映射
    - 一个 string instanceName 属性，记录实例名称，便于用户调试

- 调试结构操作：kDisplay, kAssert
    - 打印调试信息 kDisplay
        - 第一个操作数为使能信号，只有该信号为高时，kDisplay 输出
        - 之后跟随的可变数量的操作数，表示参与输出的变量
        - 一个 string formatString 属性，表示输出格式化字符串
    - kAssert
        - 一个操作数，表示断言条件

- DPI 操作：`kDpicImport` / `kDpicCall`
    - `kDpicImport`
        - 描述 `import "DPI-C"` 声明本身，不产生 operands/results
        - 通过 Operation 的 `symbol` 字段暴露唯一标识，供 `kDpicCall` 引用
        - 字符串属性
            - `dpiSymbol`：记录 `import "DPI-C"` 中暴露给 C 端的符号名
            - `svName`：记录 SystemVerilog 侧的可见名，便于回溯
        - 布尔属性
            - `isTask`：区分 `function` / `task`
            - `isContext`：对应 `context` 关键字
            - `isPure`：对应 `pure` 关键字
        - 额外 attribute `cSignature`（string）缓存 `extern "C"` 的原型描述，方便后端直接生成绑定代码
    - `kDpicCall`
        - 第一个 operand 固定为调用的使能信号，仅当为高电平时才触发后续语义
        - 之后的 operands 与 SystemVerilog 形参（一一对应的 `input` / `inout`）按照声明顺序排布
        - Operation 的 results 建模 `output` / `inout` 形参以及函数返回值，保持与源码相同的出现顺序（函数返回值排在首位）
        - 通过字符串属性 `targetImportSymbol` 记录被调用 `kDpicImport` Operation 的 `symbol`，据此解析调用目标；前端需保证引用合法
        - 额外记录两个 vector 属性，帮助还原形参/返回值在 DPI 声明中的确切位置
            - `operandArgPositions`：与 operands 一一对应，索引 0 固定为使能信号并使用值 `-1`；其余条目为 `int64_t`，记录该 operand 映射到的形参下标（基于 `kDpicImport` 声明，0 表示第一个 SystemVerilog 形参）
            - `resultArgPositions`：与 results 一一对应，`int64_t` 类型；若结果表示函数返回值则标记为 `-1`，否则填写其对应的 `output`/`inout` 形参下标


# 边 - Value

- 满足静态单赋值SSA特性，只能由一个 Operation 写入，可以被多个 Operation 读取
- 具有一个字符串类型的 symbol 字段，用于识别信号
- 具有一个 64 bit 无符号整型的 width 字段（`uint64_t` 实现），表示 Value 的位宽；位宽可大于 64 bit，只要能够落在 `uint64_t` 表示范围内即可
- 常量 Value 在 width 大于 64 bit 时必须配合 `wideValue` 属性存储具体数值；其他 Operation 依赖 width 元数据传播位宽
- 具有一个标记有无符号的布尔字段 isSigned
- 具有一个标记是否为模块输入的布尔字段 isInput
- 具有一个标记是否为模块输出的布尔字段 isOutput
- 可选记录一个 sourceLoc 字段，类型为 `std::map<std::string, std::any>`，字段含义同 Operation 的 `sourceLoc` 属性
- GRH 的数据类型不支持数组和结构体，数据和结构体均被前端扁平化，涉及访问操作通过 kSlice 和 kConcat 实现，但不能破坏SSA特性
- 基于单驱动 SSA 模型，所有输入端口和内部连接都不允许表达三态（Z）或 inout 语义；若源代码包含三态逻辑，前端需在转换前拆解为等效的多路选择或显式上拉/下拉结构
- 具有一个 defineOp 指针，指向写入 Op，若 Value 是模块的输入参数，则为空指针
- 具有一个 userOps 数组，元素为 `<Operation*, size_t operandIndex>` 二元组，记录该 Value 在各 Operation 中作为第几个操作数被引用；同一 Operation 多次使用同一个 Value 时会存储多个条目
- Graph 创建 Value 时即拥有其生命周期，禁止在多个 Graph 之间共享 Value。`defineOp` 和 `userOps` 都只能引用同一个 Graph 的 Operation。
- `isInput`/`isOutput` 会由 `Graph::addInputPort` / `Graph::addOutputPort` 自动设置，禁止手动篡改以免与端口映射失联。

# 图 - Graph

- 具有一个 moduleName 字段标识模块名称
- 具有一个 inputPorts 字段，类型为 <std::string，Value*> 的字典，记录所有输入端口
- 具有一个 outputPorts 字段，类型为 <std::string，Value*> 的字典，记录所有输出端口
- 具有一个 isTopModule bool 字段，标识是否为顶层模块
- 具有一个 isBlackBox bool 字段，标识是否为黑盒模块
- 可选记录一个 sourceLoc 字段，类型同前文所述
- 具有一个 values 数组，保存所有 value 的指针。Graph 对 values 拥有所有权，在 Graph 析构时销毁全部 values，并维持插入顺序用于遍历
- 具有一个 ops 数组，保存所有 operation 的指针。Graph 对 ops 拥有所有权，在 Graph 析构时销毁全部 operation，并尽量保持与 values 一致的拓扑顺序
- `inputPorts` / `outputPorts` 使用 `std::unordered_map` 存储，端口名唯一、查找为 O(1)。重新插入同名端口会覆盖旧记录，因此前端需要显式地维护端口定义顺序（例如额外保存一个端口名数组）。调用 `addInputPort` / `addOutputPort` 会同步设置对应 Value 的 `isInput` / `isOutput`。
- `createValue` / `createOperation` 返回的原始指针在 Graph 生命周期内始终有效，由 Graph 内部的 `std::unique_ptr` 数组管理内存；禁止在 Graph 外释放。
- 所有 Operation 的 operands 和 results 都必须引用当前 Graph 管理的 Value，禁止跨 Graph 取值，以确保 Netlist 遍历时可以通过 Graph 边界判定层次结构。
- 模块端口只支持input/output，其他特性不予支持

# 网表 - Netlist

- 具有一个可以按照 moduleName 索引全部 Graph 的容器，要求 moduleName 在网表中唯一
- 通过 Graph 的 isTopModule 字段标记顶层模块，至少存在一个顶层 Graph，且顶层 Graph 不允许被其他 Graph 的 instance 引用
- instance Operation 的 attributes 中记录被例化模块的 moduleName，运行时通过该索引解析被例化 Graph；禁止跨网表引用
- 允许存在未被引用的 Graph（如库模块），但综合或导出流程默认仅从顶层 Graph 开始遍历可达子图
- 层次结构由 Graph 内的 instance Operation 建模，网表自身不额外存储层次边

# 表达能力

## 支持的 SystemVerilog 特性

- 支持 Verilog 和 SystemVerilog 的可综合子集，所有输入都会先经 `slang` elaboration，再在 GRH 中以 SSA 形式建模。
- 支持所有常见组合算术/逻辑/移位/比较/缩减/条件/拼接/复制操作，对应 `kAdd`~`kReplicate` 等 OperationKind，能够覆盖 `assign`、`always_comb` 等组合语句。
- 支持静态/动态/数组分片、结构体/数组的扁平访问，分别使用 `sliceKind=static/dynamic/array` 的 `kSlice` 与 `kConcat`，可表达 packed/unpacked 数据的逐位运算。
- 支持同步/异步复位寄存器以及 `posedge`/`negedge`/`edge` 三种触发方式，涵盖 `always_ff`/`always @(posedge …)` 中的单时钟寄存器语义。
- 支持片上存储器（`kMemory` + 读写端口），读端口异步、写端口同步，可建模多端口 RAM、ROM 以及跨时钟域访问（结合寄存器建模）。
- 支持层次实例化，通过 `kInstance` 记录端口映射，可表达层次模块、参数化展开后的各类子模块。
- 支持常见验证/调试原语：`kDisplay` 对应 `$display` 类系统任务，`kAssert` 对应断言语句，方便在图级别保留验证语义。
- 支持 `import "DPI-C"` 函数/任务声明与调用（`kDpicImport`/`kDpicCall`），保留 `dpiSymbol`、`svName`、纯度、上下文等属性，并通过 `kDpicCall` 引用 `kDpicImport`，便于继续生成 DPI 绑定逻辑。

## 不支持/不保留的 SystemVerilog 特性

- 不支持 `#` 延迟、`wait #t` 等基于仿真时间的语句，GRH 仅建模零延迟组合和时序逻辑。
- 不支持三态驱动、`inout` 端口或多驱动 `wire`，SSA Value 只允许单驱动，所有三态需在前端转换为多路选择/上拉下拉结构。
- 不保留数组/结构体/联合等聚合类型，必须在前端扁平化后使用 `kSlice`/`kConcat` 来表示元素访问，无法在 GRH 中表达原生聚合语义。
- 模块端口只支持 `input`/`output`，因此不保留 `ref`、`interface`/`modport`、`virtual interface` 等高级端口特性，需要在前端展开为普通信号才能映射到 GRH。
- Value 仅记录位宽（`uint64_t width`）和有符号属性，不保留 `real`/`shortreal`/`time`/`chandle`/`string` 等非位向量数据类型，需要在构图前转换或报错。
- 不支持双向 DPI（例如 `export "DPI-C"`）、PLI、VPI、SDF 注入、`force/release` 等需要仿真调度器配合的语义。
- 不支持 `export "DPI-C"` 和 `export "DPI-C" task/function`
- 不支持 `cover property`/`covergroup` 等覆盖率结构
