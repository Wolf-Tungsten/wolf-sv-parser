# Transform 引用缓存改进阶段报告

## 完成内容
- 为 Value/Operation/Graph/Netlist 引入 symbol + 指针双轨引用：`definingOpSymbol/Ptr`、`ValueUser.operationSymbol/Ptr`、操作数/结果的指针缓存，并提供 `rehydratePointers()` 在 move/JSON parse 后统一重建。
- GRH API 统一使用引用入口维护关系（add/replaceOperand/Result 直接同步 symbol+ptr），Range 访问返回缓存解引用，避免热路径重复查找。
- emit/elaborate/test 路径改为使用缓存指针，修正实例输出逻辑；HDLBits DUT=026 实例名冲突通过实例名唯一化（基名 + 自增后缀）解决。

## 风险与兼容性
- symbol 仍是序列化与拷贝的金标准；指针缓存依赖 `rehydratePointers()`，若新增自定义构造需确保调用。
- 实例名唯一化会改变生成的模块实例名（仅在同名重复时附加 `_n`），但基名保持属性/符号稳定。

## 测试
- `cmake --build build -j4`
- `ctest --test-dir build --output-on-failure`
- `make run_hdlbits_test DUT=026`
