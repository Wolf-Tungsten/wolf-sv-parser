# Transform 基础设施 OKR


# Objective 1

> 目标：改进 GRH 相关数据结构中的引用关系，为快速检索做好准备，并在 transform/pass 内保持可复制、可序列化的金标准引用。

## 原则与顺序
- 以 symbol 为唯一权威标识，指针仅作缓存与便捷访问；序列化、拷贝、等价性判断都基于 symbol。
- 命名统一：符号字段统一为 `xxxSymbol`，指针缓存统一为 `xxxPtr`；迁移/克隆后集中重建指针，不留隐式悬垂。
- 施工顺序：补齐数据结构与内部 API → 升级构造/查找/遍历接口 → 更新调用点与测试，逐步验证性能与正确性。

## KR1：Operation 与 Value 互引及 Graph 引用同时保留 symbol 与指针
- Value 增加 `definingOpPtr`，`ValueUser` 记录 `opSymbol + opPtr + operandIndex`，Graph 访问器能返回两类引用以便 transform 快速遍历 def/users。
- Operation 维护 `operandsSymbols/resultsSymbols` 与 `operandsPtrs/resultsPtrs` 双轨；Graph 保持以 symbol 为 key 的存储，但提供内部 `resolve*Ptr` 钩子填充/校验缓存。
- Netlist/Graph 的 move/clone/JSON 导入结束后，统一调用重挂流程（如 `rehydratePointers()`），确保缓存与 symbol 对齐并清理悬垂指针。

## KR2：指针缓存作为加速路径，读取接口返回引用避免拷贝
- `operandValue`/`resultValue`/`operands()`/`results()` 等接口改为返回 `Value&`/`const Value&` 视图或引用，内部直接解引用缓存；缓存缺失时按 symbol 查找并回填。
- 为常用反向查询（users 列表、definingOp）提供基于指针缓存的便捷访问器，减少 transform 热点中的字符串构造与哈希查找。
- 调试场景添加轻量断言/检查，比较缓存指针与 symbol 反查结果，发现漂移时立即修正或抛出。

## KR3：设置 operand/result/user 的入口用引用，内部同步 symbol 与指针
- `add/replaceOperand`、`add/replaceResult`、`Value::setDefiningOp`、`Value::addUser/removeUser` 等接口统一接受 `Operation&`/`Value&`，内部同时写入 symbol 和指针缓存。
- Graph 的端口绑定、Netlist 的 alias/top 注册与 transform 构造路径使用引用风格，删除外部手工填 symbol 的调用点；仅 JSON 解析等特殊路径使用受限符号版私有辅助。
- 更新所有调用者（elaborate/emit/transform/tests），避免重复查找或字符串拷贝，确保关系只在一个入口维护。

## KR4：symbol 作为金标准，复制/序列化依赖 symbol 重建关系
- Graph/Netlist 拷贝或 transform 中克隆节点时只复制 symbol 与属性，指针缓存通过集中钩子重建；禁止裸 memcpy 导致悬垂。
- emit/transform/JSON dump/pretty 打印等输出路径以 symbol 生成关系，确保缓存缺失也能 roundtrip；必要时在拷贝路径上修复/补充 def 与 users。
- 补充测试：构造含多用户与替换操作的图，验证移动后缓存重建、def/users 与 operands/results 双向一致，`ctest` 回归通过且性能热点查找次数下降。
