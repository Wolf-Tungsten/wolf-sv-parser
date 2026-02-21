# Transform 阶段小结（ConstantFold 引入）

## 完成事项
- 新增 `const-fold` pass：基于 `slang::SVInt` 的纯组合常量折叠，迭代收敛，支持算术/逻辑/位运算、`assign/concat/replicate/slice/mux` 等，遇到 X/Z 可配置跳过。
- 折叠后生成稳定命名的 kConstant 节点，并在同轮内重定向 users 与模块输出端口绑定，再删除被折叠的原 op，保持 Graph 拓扑整洁。
- 强化安全性：替换 operands 时检测越界并记录诊断，整个 pass 入口包裹异常捕获以避免崩溃；错误/警告遵循 pass diagnostics。
- Graph 能力扩展：支持输出端口重新绑定（`replaceOutputValue`）与安全删除 op（同步清理 users/def/operationOrder）。
- 文档与测试：新增维护者指南 `docs/transform/const_fold.md` 描述实现细节；变换测试覆盖多轮折叠、X 跳过、缺属性报错、输出重定向。

## 已执行验证
- `cmake --build build --target transform-const-fold`
- `./build/bin/transform-const-fold`

## 已知限制 / 后续计划
- 四态折叠策略简单（允许/禁止 X 传播二选一），未实现更精细的分支敏感折叠。
- 不尝试去重常量节点；如需 DAG 合并可引入常量池或 CSE。
- 仍保留旧 Value（仅删除折叠后的 op），后续可考虑垃圾回收未使用 Value/修复 users 统计。
