# 阶段26：for 迭代常量与 net 初值

## 完成内容
- **KR1（for/foreach 索引稳态）** 允许无 inline loopvar 的 `for` 初始化赋值，`step` 为赋值时会写回 EvalContext；`AlwaysBlockRHSConverter` 优先返回 loop scope 中的迭代常量并在命中自定义值时跳过 RHS 缓存，确保每轮迭代都能看到当前索引。
- **KR2（guard 合并）** `pushGuard` 会把新 guard 与外层 guard 做 `kLogicAnd` 后入栈并过滤空值，case default/if 分支在顺序 always 中自动继承完整的 enable 条件。
- **KR3（net 初值写回）** `processNetInitializers` 扫描模块内带 initializer 的 wire，使用 Comb RHS 转换初值并通过 WriteBackMemo 生成连续赋值；`collectSignalMemos` 在无显式驱动时也将此类 wire 保持在 netMemo 里以便 RHS 解析。

## 额外修复
- `deriveParameterSuffix` 忽略 localparam，避免纯编译期常量污染图名；`reportUnsupported` 补充 NamedValue 的符号名便于定位。

## 测试记录
- 未运行（未收到指令）。

## 后续建议
1. 补充覆盖 net initializer 写回与 seq/comb for 循环索引更新的单元测试，验证跨迭代不会复用旧 RHS 缓存。
2. 观察 guard 合流后的 kLogicAnd 生成是否需要进一步去重/常量折叠，避免深层嵌套时产生多余节点。
