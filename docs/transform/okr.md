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