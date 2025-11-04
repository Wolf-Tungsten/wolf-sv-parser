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

## Operation 的粗粒度分类描述

- 常量定义操作
    - 没有 operand Value，只有一个输出 result Value
    - 常量 symbol 应尽可能可读，尽可能沿用用户输入代码的命名
    - 常量位宽以 uint64_t 类型名为 width 的 attribute 标识
    - 常量值的记录方法
        - 使用 bool signed attribute 标识是否为有符号常量
        - 使用 bool wide attribute 标识是否为位宽大于 64 bit 的值
        - 当常量值位宽小于等于64位时，使用 int64_t 或 uint64_t 的 value 属性直接保存
        - 当常量值位宽大于64位时，使用 vector<int64_t> 或 vector<uint64_t> 的 wide_value 属性存储常量值，小端序形式，索引 i 保存 [i*64 +: 64] 位

- 组合逻辑操作
    - 支持 SystemVerilog 的算术操作符、相等操作符、逻辑操作符、按位操作符、缩减操作符、移位操作符、关系操作符、条件操作符、拼接和复制操作符
    - 所有操作符的操作数都用 operand 传入
    - 条件操作使用 2选1 MUX operation 建模，具有三个operand，第一个为判断条件，第二个为条件真时结果，第三个为条件假时结果
    - 拼接操作的 operand 数不固定，拼接结果为 operand 从低索引到高索引、从右至左拼接而成
    - 复制操作中，被复制的变量以 operand 方式传入，副本数量用 uint64_t rep attribute 保存

- 位截取操作
    - 在 SystemVerilog 操作符基础上添加三个位截取操作 SLICE 、SLICE_D 、SLICE_A
    - SLICE 为静态截取，具有一个操作数 o，一个输出结果 r，一个 uint64_t start 属性表示截取截取起点，一个 uint64_t end 属性表示截取终点，end 包含在截取范围内， r = o[end : start];
    - SLICE_D 为动态截取，具有两个操作数，其中第一个操作数 a 为被截取输入，第二个操作数 b 指示截取起点，具有一个输出结果 r，一个 uint64_t width 属性表示截取位宽 r = a[b +: width];
    - SLICE_A 为按数组截取，具有两个操作数，其中第一个操作数 a 为被截取输入，第二个操作数 i 为数组索引，一个输出结果 r，一个 uint64_t width 属性表示数组中元素位宽 r = a[i * width +： width]；
    - 为了便于分析，GRH 中的 Value 不保留数组结构信息，对于数组乃至多维数组的动态访问使用级联的 SLICE_A 实现，静态访问使用 SLICE 实现；
    - GRH 中的 Value 也不保留结构体信息，对结构体的访问使用 SLICE 实现。

- 时序逻辑部件操作
    - 寄存器: REG 和 REGA
        - REG 操作建模同步复位寄存器
            - 操作数 1 为时钟信号
            - 操作数 2 为 d 信号
            - 结果为 q 信号
            - 字符串类型的 clk_type 属性可取值 posedge、negedge、edge
        - REG_A 操作建模异步复位寄存器
            - 操作数 1 为时钟信号
            - 操作数 2 为复位信号
            - 操作数 3 为 d 信号
            - 结果为 q 信号
            - 字符串类型的 clk_type 属性可取值 posedge、negedge、edge
            - 字符串类型的 rst_type 属性可取值 posedge、negedge、edge
    - 片上mem: MEM, MEM_R_PORT, MEM_W_PORT
        - MEM 操作建模存储阵列
            - 没有操作数，也没有结果
            - uint64_t width 属性记录每行bit数位宽
            - uint64_t row 属性记录总行数
            - 设置一个全局唯一的 symbol 用于 MEM_R_PORT 和 MEM_W_PORT 索引，该 symbol 应尽可能人类可读，便于后续调试
        - MEM_R_PORT
            - 操作数 1 为读地址信号
            - 结果为读数据信号
            - 字符串类型的 mem_symbol 属性指向 MEM_R_PORT 读取的 MEM 组件
            - MEM_R_PORT 建模的读取为异步读取。如果需要同步读取，通过与 REG 或 REG_A 联合实现
        - MEM_W_PORT
            - 操作数 1 为写时钟信号
            - 操作数 2 为写地址信号
            - 操作数 3 为写数据信号
            - 字符串类型的 mem_symbol 属性指向 MEM_W_PORT 读取的 MEM 组件
            - MEM_W_PORT 建模为同步写入
            - 字符串类型的 clk_type 属性可取值 posedge、negedge、edge
        - 一个 MEM 可以关联多个读写口，以支持跨时钟域读写等场景

- 层次结构操作：INSTANCE
    - 字符串类型的 module_name 属性，用于识别被实例化的模块（也就是 Graph）
    - 可变数量的操作数，表示被实例化模块的输入
    - 一个 vector<string> inputPortName，顺序和数量与操作数一致，表示每个操作数和模块输入的关系映射
    - 可变数量的结果result，表示被实例化模块的输出
    - 一个 vector<string> outputPortName，顺序和数量与操作数一致，表示每个结果和模块输出的关系映射
    - 一个 string instanceName 属性，记录实例名称，便于用户调试

- 调试结构操作：DISPLAY, ASSERT
    - 打印调试信息 DISPLAY
        - 第一个操作数为使能信号，只有该信号为高时，DISPLAY 输出
        - 之后跟随的可变数量的操作数，表示参与输出的变量
        - 一个 string formatString 属性，表示输出格式化字符串
    - ASSERT
        - 一个操作数，表示断言条件

- DPI 操作
    - （帮我想一下怎么办？只支持 import "DPI" 即可，用操作数和结果和对应参数和返回值）


# 边 - Value

- 满足静态单赋值特性，只能由一个 Operation 写入，可以被多个 Operation 读取
- 具有一个字符串类型的 symbol 字段，用于识别信号
- 具有一个位宽字段 width
- 具有一个标记有无符号的布尔字段 isSigned
- 具有一个标记是否为模块输入的布尔字段 isInput
- 具有一个标记是否为模块输出的布尔字段 isOutput
- 具有一个 defineOp 指针，指向写入 Op，若 Value 是模块的输入参数，则为空指针
- 具有一个 userOps 数组，元素为 `<Operation*, size_t operandIndex>` 二元组，记录该 Value 在各 Operation 中作为第几个操作数被引用；同一 Operation 多次使用同一个 Value 时会存储多个条目

## 图 Graph

- 具有一个 module_name 字段标识模块名称
- 具有一个 inputPorts 字段，类型为 <std::string，Value*> 的字典，记录所有输入端口
- 具有一个 outputPorts 字段，类型为 <std::string，Value*> 的字典，记录所有输出端口
- 具有一个 isTopModule bool 字段，标识是否为顶层模块
- 具有一个 isBlackBox bool 字段，标识是否为黑盒模块
- 具有一个 values 数组，保存所有 value 的指针。Graph 对 values 拥有所有权，在 Graph 析构时销毁全部 values，并维持插入顺序用于遍历
- 具有一个 ops 数组，保存所有 operation 的指针。Graph 对 ops 拥有所有权，在 Graph 析构时销毁全部 operation，并尽量保持与 values 一致的拓扑顺序

# 网表

- 具有一个可以按照 moduleName 索引全部 Graph 的容器，要求 moduleName 在网表中唯一
- 通过 Graph 的 isTopModule 字段标记顶层模块，至少存在一个顶层 Graph，且顶层 Graph 不允许被其他 Graph 的 instance 引用
- instance Operation 的 attributes 中记录被例化模块的 moduleName，运行时通过该索引解析被例化 Graph；禁止跨网表引用
- 允许存在未被引用的 Graph（如库模块），但综合或导出流程默认仅从顶层 Graph 开始遍历可达子图
- 层次结构由 Graph 内的 instance Operation 建模，网表自身不额外存储层次边

## Operation 的粗粒度分类

- 常量定义
- 组合逻辑操作：算数、布尔、移位、多路复用器、信号切片读取、信号拼接
- 时序逻辑部件：reg、mem、mem_read_port、mem_write_port
- 层次结构部件：instance
- 调试结构部件：display，assert，dpic 等