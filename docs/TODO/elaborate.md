# Elaborate

wolf-sv-compiler 的 elaborate 过程将 slang 的 AST 转换为 wolf_sv::Netlist

class Elaborate 完成上述建图转换过程，管理过程中所需的数据结构，支持线程安全

class Elaborate 的 convert 方法接收 slang 的 auto &root = compilation->getRoot() 作为入参，返回一个 wolf_sv::Netlist*

## 从顶层模块开始

convert 方法中遍历所有的顶层模块，对每个顶层模块调用 processTopModule

在 slang 的 ast 中，顶层模块是一个 Instance，所以 processTopModule 内部调用 processInstance

## processInstance

processsInstance 进一步分解为两部分

- processInstanceBody: 解析模块体
- processInstanceConnection：解析实例的连接

## processInstanceBody

- wolf-sv-compiler 为每个 InstanceBody 建立一个独立的图。
