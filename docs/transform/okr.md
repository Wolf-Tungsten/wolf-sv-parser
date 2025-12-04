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
- KR3: GRHVerifyPass 检查并尝试修复 op 和 value 连接关系的合法性，先通过 symbol 检查存在性，不存在则无法修复直接报错；再检查 ptr 指向与 symbol 指向是否一致，若不一致则修复 ptr