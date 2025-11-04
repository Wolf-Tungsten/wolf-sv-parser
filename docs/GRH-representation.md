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
- 具有一个 attributes 字典，key 为字符串类型，value 为 std::any，记录 Operation 的元数据。attributes 的 value 仅允许存储 `bool`、`int64_t`、`uint64_t`、`double`、`std::string`、`std::vector<std::any>`、`std::map<std::string, std::any>` 等 JSON 兼容类型，序列化时按照 JSON 语义写出，遇到不受支持的类型时抛错。`std::vector<std::any>` 与 `std::map<std::string, std::any>` 的元素同样必须受上述 JSON 限制，不得再嵌套其他自定义类型，以便反序列化逻辑只需处理基本数值/字符串/容器组合。推荐在文档中约定每个 attribute 的键名与类型，避免使用运行时才判定的“任意字典”形式。
- 所有 Operation/Value/Graph 都可以携带一个可选的 `sourceLoc` 属性，用 `std::map<std::string, std::any>` 结构表示；推荐字段包括 `file`（字符串，源文件路径）、`line`（uint64_t，行号）、`column`（uint64_t，列号）、`hierPath`（字符串，层次路径）。`line`/`column` 采用 1 基计数，`hierPath` 建议使用 `.` 分隔层次（例如 `top.u_sub.u_leaf`）。缺失字段表示对应信息未知

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
    - 常量位宽、符号和值均以 Operation attributes 记载，是下游恢复字面量的权威来源；对应的 result Value 只需复制 `width`/`isSigned` 等类型信息
- 常量值的记录方法（遵循“仅保留恢复语义所需的最少属性”原则）
    - 使用 bool signed attribute 标识是否为有符号常量；为避免歧义，显式写出 true/false，而非依赖缺省值
    - 仅当常量位宽大于 64 bit 时写入 bool wide attribute 并设置为 true；位宽不超过 64 bit 时省略该属性
    - 当常量值位宽小于等于 64 bit 时，只允许写入一个 `value` 属性（类型为 int64_t 或 uint64_t，根据 `signed` 选择），并且必须省略 `wideValue`
    - 当常量值位宽大于 64 bit 时，只允许写入一个 `wideValue` 属性（类型为 vector<int64_t> 或 vector<uint64_t>，与 `signed` 一致），并且必须省略 `value`。`wideValue` 采用小端序，索引 i 对应 [i*64 +: 64] 位；最高块仅保留真实有效位并遵循符号扩展/零扩展规则，禁止额外填充

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
    - 位宽/符号语义遵循 SystemVerilog 表达式规则，前端在构建图时即根据 SV 的上下文确定每个 Value 的 `width` 和 `isSigned`。除非另有说明，算术/关系/按位操作均先将所有 operand 对齐到同一宽度：取参与 operand 的最大位宽 `W`，再按 `isSigned` 进行符号扩展（signed）或零扩展（unsigned）到 `W` 位。常见操作的特化约定如下：
        - `kAdd`/`kSub`：结果宽度为 `W`，溢出会被截断；只有所有 operand 均为 signed 时结果才视为 signed。
        - `kMul`：结果宽度为 `width(lhs) + width(rhs)`，并保持“全部 signed 才 signed”规则。若上层需要较窄位宽，需显式加入 `kSlice`。
        - `kDiv`/`kMod`：被除数（Dividend）保持自身宽度，`kDiv` 结果宽度等于被除数，`kMod` 结果宽度等于除数；两者的 `isSigned` 仅在所有参与 operand signed 时为 true。
        - `kEq`、`kNe`、`kLt`、`kLe`、`kGt`、`kGe`：结果宽度固定为 1，`isSigned=false`，比较前按上述规则扩展 operand。
        - `kLogicAnd`、`kLogicOr`、`kLogicNot`：输入先按“非零即真”收敛为 1 bit，再进行逻辑运算，结果宽度为 1、`isSigned=false`。
        - `kAnd`、`kOr`、`kXor`、`kXnor`、`kNot`：结果宽度为 `W`，`kNot` 继承其唯一 operand 的 `isSigned`，其余操作仅当所有 operand signed 时结果才为 signed。
        - `kReduce*`：结果宽度为 1，`isSigned=false`。
        - `kShl`、`kLShr`、`kAShr`：被移位 operand 的宽度保持不变。`kShl` 和 `kLShr` 总是零填充；`kAShr` 会在 `isSigned=true` 时复制符号位，否则零填充；移位量 operand 的具体位宽不影响语义。
        - `kMux`：第一个 operand 为条件，按逻辑运算规则收敛为 1 bit；两个数据 operand 按 `W = max(width0, width1)` 扩展，结果 `width=W`，`isSigned` 仅当两个数据 operand 均为 signed 时为 true。
        - `kConcat`：结果宽度为所有 operand 宽度之和，拼接顺序为“operand 索引从低到高、每个 operand 内从高位到低位”，结果恒为 unsigned（`isSigned=false`）。
        - `kReplicate`：结果宽度为 `rep * width(operand)`，结果 `isSigned=false`。
        - `kSlice`：结果宽度由对应 attribute 明确给出；是否视为 signed 由前端根据 SV 上下文设置 `isSigned`，默认推荐设为 false 以匹配 SV 对切片的 unsigned 语义。

- 位截取操作
    - 所有位截取均复用 `kSlice` Operation，通过字符串属性 `sliceKind` 区分模式，取值 `static`（静态）、`dynamic`（动态）、`array`（数组）。默认约定 Bit 0 为 LSB、Bit `width-1` 为 MSB，所有 `start`/`end`/`width`/索引参数均在该编号体系下解释。
    - `static`：一个 operand `o`，一个输出 `r`。必须提供 `sliceStart`/`sliceEnd`（`uint64_t`）两个 attribute，均包含端点，语义 `r = o[sliceEnd : sliceStart]`，并要求 `sliceStart <= sliceEnd`。`sliceStart=0` 表示最低位，结果宽度可由 `sliceEnd - sliceStart + 1` 推导，禁止额外重复填写 `width` 以避免两个字段冲突。
    - `dynamic`：两个 operanda（`a` 为被截取输入，`b` 为截取起点），一个输出 `r`。必须提供 `sliceWidth`（`uint64_t`） attribute，语义 `r = a[b +: sliceWidth]`。若截取区间超出 `a` 的有效位范围，构图流程必须报错并拒绝生成该 Operation，不允许静默截断或填充。
    - `array`：两个 operanda（`a` 为被截取输入，`i` 为数组索引），一个输出 `r`。同样使用 `sliceWidth`（`uint64_t`） attribute，语义 `r = a[i * sliceWidth +: sliceWidth]`。若 `a` 由多维数组或结构体扁平化而来，约定按 SystemVerilog 的“右侧维度靠近 LSB，向左逐级拼接”顺序展开，`i=0` 总对应最低有效的元素切片；若索引导致访问越界，同样必须报错。
    - 为了便于分析，GRH 的 Value 不保留数组或结构体拓扑信息；数组（含多维）访存通过级联 `sliceKind=array` 完成，结构体访问使用 `sliceKind=static`。若需要不同的位序，可在前端通过适当的 `kConcat`/`kSlice` 组合进行转换。

- 时序逻辑部件操作
    - 寄存器: OperationKind::kRegister
        - 操作数排列
            - operand 0：时钟信号，所有寄存器都必须提供
            - operand 1：复位信号，仅当 `resetCategory` 出现时提供；`resetCategory=sync` 和 `resetCategory=async` 均复用同一位置
            - operand 2：复位值（`resetValue`），仅当存在复位时提供，必须由 `OperationKind::kConstant` 生成，并与寄存器输出位宽/符号属性一致
            - 最后一个 operand：数据输入 d（若存在复位信号，则为 operand 3，否则为 operand 1）
            - Operation 的唯一 result 表示寄存器输出 q，其位宽和 `isSigned` 与 d 一致
        - 当存在复位时，operand 2 提供的 `kConstant` Operation 即定义了 `resetValue`，不再通过属性重复描述常量内容。
        - 属性
            - `resetCategory`：字符串，可选取值 `sync` / `async`，缺省表示“无复位”，此时寄存器只在时钟沿上采样 d
            - `clkType`：字符串，取值 `posedge`、`negedge`、`edge`，描述触发沿；其中 `edge` 表示对同一时钟信号的正、负沿都触发（等价于 `always @(posedge clk or negedge clk)`）
            - `rstType`：仅当 `resetCategory=async` 时使用，取值同 `clkType`，对应 `always @(posedge clk or negedge rst)` 中的复位触发沿；若取值为 `edge`，表示任意沿都会触发异步复位
            - `rstPolarity`：当存在复位（同步或异步）时必须设置，取值 `activeHigh`/`activeLow`，指明复位信号的有效电平
            - `initMethod`（string）：必填，取值 `zero` / `one` / `random`，明确寄存器在上电且尚未执行复位时的初值语义
        - 语义
            - 无复位：寄存器上电瞬间按照 `initMethod` 指定的方式初始化；此后每逢 `clkType` 指定的时钟沿，q ← d。
            - 同步复位：寄存器上电瞬间按照 `initMethod` 指定的方式初始化；每逢时钟沿，先检查复位信号是否处于 `rstPolarity` 指定的有效电平，若有效则 q ← operand2（`kConstant`）提供的 `resetValue`，否则 q ← d。
            - 异步复位：寄存器上电瞬间按照 `initMethod` 指定的方式初始化；当复位信号出现 `rstType` 指定的沿时立即更新 q ← operand2（`kConstant`）提供的 `resetValue`，并在复位信号维持 `rstPolarity` 指定的有效电平期间保持该值；在时钟沿上若复位信号处于无效电平，则 q ← d。
        - 不额外建模时钟使能，若需要使能语义，应在构图前将 `d` 端构造成多路选择或逻辑门以表达使能行为
    - 片上 mem: OperationKind::kMemory、OperationKind::kMemoryReadPort、OperationKind::kMemoryWritePort
        - kMemory 操作建模存储阵列
            - Operands / Results：均为空；该 Operation 仅作为“存储体”占位，并通过 `symbol` 字段被读写端口引用
            - Attributes
                - `width`（uint64_t）：每一行（word）的 bit 宽度
                - `row`（uint64_t）：总行数，决定寻址空间
                - `maskGranularity`（uint64_t，可选）：当指定时，表示写入 mask 的粒度（单位：bit），必须整除 `width`。缺省或取值 0 表示该存储体不支持部分写，使能 mask operand 应被省略
                - `initMethod`（string）：必填，取值 `zero` / `one` / `random`，描述上电时全部存储行的逻辑初始内容
            - 约束：`symbol` 必须在整个 Graph 内唯一，且是 kMemoryReadPort / kMemoryWritePort 的唯一关联依据
        - kMemoryReadPort（异步读）
            - Operands
                - operand 0：读地址信号，位宽需覆盖 `row` 的寻址空间；若不足，读者需在前端对地址进行截断/扩展，禁止依赖下游自动截断
            - Results
                - result 0：读数据信号，位宽固定为所关联 kMemory 的 `width`，`isSigned=false`
            - Attributes
                - `memSymbol`（string）：指向目标 kMemory 的 `symbol`
            - 语义：地址变化后，输出立即反映相应存储行的内容；若需要同步读，请将输出接入设置了 `resetCategory=sync` 的 kRegister
        - kMemoryWritePort（同步写）
            - Operands
                - operand 0：写时钟信号，必需
                - operand 1：写地址信号，位宽同读端口要求
                - operand 2：写使能信号，宽度固定为 1。低电平时，当前时钟沿不对存储体产生影响
                - operand 3：写数据信号，位宽必须与目标 kMemory 的 `width` 一致，`isSigned=false`
                - operand 4（可选）：写 mask 信号，仅当目标 kMemory 的 `maskGranularity` 为正值时出现，位宽为 `width / maskGranularity`。第 i 位为高表示写入 operand3 中 `[i*maskGranularity +: maskGranularity]` 区间，其中 `i=0` 对应最低有效位分块；各 mask 位独立决定是否覆盖各自的区间
            - Results：无
            - Attributes
                - `memSymbol`（string）：指向目标 kMemory 的 `symbol`
                - `clkType`（string）：取值 `posedge` / `negedge` / `edge`，描述写入触发沿；其中 `edge` 表示对同一时钟的正、负沿都执行写入
            - 语义：在选定的时钟沿上（若 `clkType=edge` 则正、负沿都生效），若写使能为高，则写端口将 operand3 写入地址 operand1 对应的存储行；当提供写 mask 时，仅更新 mask 对应的粒度区间，其余 bit 保持原值；若 mask 全为 0，则本次写入不对存储体产生任何修改
        - 一个 kMemory 可以关联多个读写口，以支持多端口或跨时钟域访问；前端需负责冲突检测与顺序语义建模
        - 地址/容量约束：`row` 不要求为 2 的幂，但地址 operand 的位宽必须不少于 `ceil(log2(row))`，多出的高位可视作未使用；地址一旦超出 `row`-1 的范围或未按照 `maskGranularity` 对齐，其读写结果未定义，需在前端消除。
        - 竞争/冲突约束：若多个写端口在同一时钟沿写入同一地址，或写入与同周期读建立冲突，GRH 不定义结果；前端应在建图前禁止该类情况，下游实现可直接报错以暴露设计问题。
        - 初始化：所有地址在仿真/综合入口时均按照 `initMethod` 指定的策略生成初值；`random` 表示实现相关的伪随机填充，但同一个实现必须保持可重复性

- 层次结构操作：kInstance
    - 字符串类型的 moduleName 属性，用于识别被实例化的模块（也就是 Graph）
    - 可变数量的操作数，表示被实例化模块的输入
    - 一个 vector<string> inputPortName，顺序和数量与 operands 一致，表示每个操作数和模块输入的关系映射；`inputPortName[i]` 必须与被实例化 Graph 中真实存在的端口名完全匹配，同一端口不得重复出现。若某个端口需要拉高/拉低，显式构造相应的常量/Value；若省略某个输入端口，则等效于连接一个与端口位宽一致的常量 0，前端在构图或序列化时必须显式补齐该常量
    - 可变数量的结果result，表示被实例化模块的输出
    - 一个 vector<string> outputPortName，顺序和数量与 results 一致，表示每个结果和模块输出的关系映射；每个输出端口最多出现一次。若省略某个输出端口，则默认视为该输出被上层忽略，若上层需要占位值，应显式连接到常量 0，不应依赖隐式悬空
    - 一个 string instanceName 属性，记录实例名称，便于用户调试
    - GRH 不保留参数化信息。所有 `parameter` / `localparam` / `defparam` 都应在前端（slang AST）阶段完全求值，不同参数实参组合必须展开为不同的 Graph，并在 Netlist 中以独立的 `moduleName` 注册；`kInstance` 仅通过 `moduleName` 绑定目标 Graph，不再携带额外的参数属性。

- 调试结构操作：kDisplay, kAssert
    - 打印调试信息 kDisplay
        - 第一个操作数为使能信号，只有该信号为高时，kDisplay 输出
        - 之后跟随的可变数量的操作数，表示参与输出的变量
        - 一个 string formatString 属性，表示输出格式化字符串；语法与 SystemVerilog `$display` 一致，支持 `%b/%d/%h/%0d/%0h/%x/%0t` 等常见占位符，默认十进制宽度规则同 SV
        - 操作数会依次替换格式串中的占位符；占位符数量不足会触发构图错误，若多余操作数则被忽略
        - 语义：在触发该 Operation 的调度点，将格式化文本追加到仿真/日志输出；Operation 不产生结果，仅有副作用
    - kAssert
        - 一个操作数，表示断言条件
        - 语义：条件为 0（逻辑假）时立即报告断言失败，默认等同于 SystemVerilog `assert` 的致命行为（终止当前仿真或 pass）；若需要非致命处理，应由前端插入额外逻辑或使用 `kDisplay` 协助打印

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
        - 该使能由前端解析 slang AST 时生成，通常挂接常量 1；在建模 handshake/valid 时才接入真实逻辑，信号为 0 时完全跳过本次 DPI 调用。
        - 之后的 operands 与 SystemVerilog 形参（一一对应的 `input` / `inout`）按照声明顺序排布
        - Operation 的 results 建模 `output` / `inout` 形参以及函数返回值，保持与源码相同的出现顺序（函数返回值排在首位）。`inout` 形参会同时在 operands（输入方向）与 results（输出方向）中各出现一次，两个 Value 不共享引用，以满足 SSA 的单驱动原则；前端需显式连线，将 result Value 作为后续操作的来源
        - 通过字符串属性 `targetImportSymbol` 记录被调用 `kDpicImport` Operation 的 `symbol`，据此解析调用目标；前端需保证引用合法
        - 额外记录两个 `std::vector<int64_t>` 属性，帮助还原形参/返回值在 DPI 声明中的确切位置，均采用 -1 作为“无对应形参/返回值”的占位
            - `operandArgPositions`：与 operands 一一对应，索引 0 固定为使能信号并写入 -1；其余条目记录该 operand 映射到的形参下标（基于 `kDpicImport` 声明，0 表示第一个 SystemVerilog 形参）。`inout` 的输入方向同样使用其在声明中的下标
            - `resultArgPositions`：与 results 一一对应；若结果表示函数返回值则填 -1，否则填写其对应的 `output`/`inout` 形参下标。`inout` 的返回方向使用与 operands 相同的形参下标，方便前端在 SSA 层面建立“读 → 写”顺序
        - 当某个形参声明为 `inout` 时，工具需校验 `operandArgPositions` 与 `resultArgPositions` 中的条目保持一致，以确保 SSA 图中读写配对


# 边 - Value

- 满足静态单赋值SSA特性，只能由一个 Operation 写入，可以被多个 Operation 读取
- Value 只记录 operation 之间传递的值的类型信息（`width`、`isSigned`、端口属性等），具体常量字面值仅保存在其定义 Operation 的 attributes 中
- 具有一个字符串类型的 symbol 字段，用于识别信号；要求在 Graph 作用域内唯一且非空
- 具有一个 `uint64_t` 类型的 width 字段，表示 Value 的位宽
- 具有一个标记有无符号的布尔字段 isSigned
- 具有一个标记是否为模块输入的布尔字段 isInput
- 具有一个标记是否为模块输出的布尔字段 isOutput
- 可选记录一个 sourceLoc 字段，类型为 `std::map<std::string, std::any>`，字段含义同 Operation 的 `sourceLoc` 属性
- GRH 的数据类型不支持数组和结构体，数组和结构体均被前端扁平化，涉及访问操作通过 kSlice 和 kConcat 实现，但不能破坏SSA特性
- 基于单驱动 SSA 模型，所有输入端口和内部连接都不允许表达三态（Z）或 inout 语义；若源代码包含三态逻辑，前端需在转换前拆解为等效的多路选择或显式上拉/下拉结构
- 具有一个 defineOp 指针，指向写入 Op，若 Value 是模块的输入参数，则为空指针
- 具有一个 userOps 数组，元素为 `<Operation*, size_t operandIndex>` 二元组，记录该 Value 在各 Operation 中作为第几个操作数被引用；同一 Operation 多次使用同一个 Value 时会存储多个条目
- Graph 创建 Value 时即拥有其生命周期，禁止在多个 Graph 之间共享 Value。`defineOp` 和 `userOps` 都只能引用同一个 Graph 的 Operation。
- `isInput`/`isOutput` 会由 `Graph::addInputPort` / `Graph::addOutputPort` 自动设置，禁止手动篡改以免与端口映射失联。
- result Value 的 `width`、`isSigned` 等元信息必须与其定义 Operation（包括 `kConstant`）保持一致

# 图 - Graph

- 具有一个 moduleName 字段标识模块名称
- 具有一个 inputPorts 字段，类型为 <std::string，Value*> 的字典，记录所有输入端口
- 具有一个 outputPorts 字段，类型为 <std::string，Value*> 的字典，记录所有输出端口
- 具有一个 isTopModule bool 字段，标识是否为顶层模块
- 具有一个 isBlackBox bool 字段，标识是否为黑盒模块
- 可选记录一个 sourceLoc 字段，类型同前文所述
- 具有一个 values 数组，保存所有 value 的指针。Graph 对 values 拥有所有权，在 Graph 析构时销毁全部 values，并维持插入顺序用于遍历
- 具有一个 ops 数组，保存所有 operation 的指针。Graph 对 ops 拥有所有权，在 Graph 析构时销毁全部 operation，并尽量保持与 values 一致的拓扑顺序
- `inputPorts` / `outputPorts` 使用 `std::unordered_map` 存储，端口名唯一、查找为 O(1)。重新插入同名端口会覆盖旧记录，本规范不要求维护额外的顺序容器。调用 `addInputPort` / `addOutputPort` 会同步设置对应 Value 的 `isInput` / `isOutput`。
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
- 不支持 PLI、VPI、SDF 注入、`force/release` 等需要仿真调度器配合的语义。
- 不支持 `export "DPI-C"` 和 `export "DPI-C" task/function`
- 不支持 `cover property`/`covergroup` 等覆盖率结构
- 不支持隐式锁存器或 `always_latch`，出现该类语句时前端直接报错，不会在 GRH 中生成等效 Operation。
