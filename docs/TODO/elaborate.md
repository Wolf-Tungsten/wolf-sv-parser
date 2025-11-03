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

wolf-sv-compiler 为每个 InstanceBody 建立一个独立的图。

在 slang 的 ast 中，InstanceBody 可以被重用。

-----

接下来，请你执行以下任务：

1. 搞清楚解析 slang 的时候，如何判断一个 InstanceBody 是否为重用的？
2. 当存在InstanceBody重用时，我该如何唯一标识这个 InstanceBody?
3. 根据问题1-2，设计一个方案，在 Elaborate 中建立一个字典结构，记录 InstanceBody 和 wolf-sv-compiler 图的映射关系

## 实施方案

- 通过 `InstanceSymbol::getCanonicalBody()` 判断 InstanceBody 是否为重用：返回非空时指向 canonical 副本，返回空时当前 Instance 自身是原始体。
- 为避免重复建图，先取得 `key = canonical ? canonical : &instance.body`，利用 InstanceBodySymbol 指针稳定性作为唯一标识。
- 在 `wolf_sv::Elaborate` 内新增 `std::unordered_map<const slang::ast::InstanceBodySymbol*, wolf_sv::Graph*> instanceBodyGraphs_`，并在 `convert` 开始时清空。
- 引入 `Graph& getOrCreateGraph(const slang::ast::InstanceSymbol&, Netlist&)` 辅助函数：根据 `key` 查询 / 创建 Graph，为新建的图记录到字典中，再返回引用。
- `processInstanceBody` 中通过该辅助函数获得图句柄，若已缓存则复用；`processInstanceConnection` 中按需访问已存在的图来补充连接。
