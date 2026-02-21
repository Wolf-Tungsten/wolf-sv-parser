# 构建 Transform 基础设施以及常用 Pass

## Objective1: 改进 GRH 相关数据结构中的引用关系，为快速检索做好准备

- KR1: Operation 和 Value 中的互相引用、和对上层 Graph 的引用，既保留通过 symbol 的引用（统一为 xxxSymbol），也保留直接指针的引用（统一为 xxxPtr）
- KR2: 直接指针作为缓存使用，getOperand 等接口返回指针的解引用（C++ 引用）以避免拷贝
- KR3: 设置 operand、result、user 的接口传入的参数应当是引用类型，以避免不必要的拷贝，这些方法内部更新symbol引用和指针引用
- KR4: 在维护指针引用的同时，也维护 symbol 引用，symbol 引用在 emit 和 transform 过程中进行拷贝复制时作为 Operation 和 Value 之间关系的金标准引用方式

## Objective2: Transform 以独立 pass 的形式实现，实现支持 pass 的基本框架

- KR1: 创建 Pass 基类，支持参数配置，之后的 Pass 从基类派生，Pass 对 Netlist 进行修改
- KR2: 创建 PassManager 类，所有 pass 按顺序注册到 PassManager 中，绑定好参数，按顺序执行
- KR3: 修改入口 main 函数，在 Elaborate 和 Emit 之间创建 PassManager，注册 Pass，执行变换流程

## Objective3: GRHVerifyPass 检查图结构合法性与完整性的Pass

- KR1: 在现有pass framework 的基础上，创建 GRHVerifyPass
- KR2: GRHVerifyPass 检查 op 的合法性，kind 和 操作数、结果数 的关系是否正确，kind 要求的 attr 是否都存在并合法，不合法则报错，输出错误情况便于排查
- KR3: GRHVerifyPass 按 symbol 检查 op 和 value 的连接关系，缺失直接报错；在 autoFixPointers 开启时解析填充 operand/result/user 指针并可从操作数重建 users 列表，不做指针与 symbol 不一致的额外校验

## Objective4: ConstantFoldPass 进行常量传播/折叠优化

- KR1: 创建 ConstantFoldPass，实现常量传播/折叠优化，当一个Op的所有操作数Value都来自于 kConstant 时，尝试进行传播
- KR2: 构建一个以 Op 为粒度的常量计算引擎，对于可折叠的 Op 生效，输入的Op满足所有操作数都是常量的约束，输出一个计算后的 kConstant，注意 SystemVerilog 的常量系统很复杂，支持任意位宽，Op 折叠结果的位宽、结果、溢出处理应当符合 SystemVerilog 的规则
- KR3: ConstantFoldPass 采用迭代-收敛模式，每轮迭代先检测，然后插入新常量节点，替换旧节点再进行下一轮迭代，直到收敛。
- KR4: 创建测试样例，测试 ConstantFoldPass
