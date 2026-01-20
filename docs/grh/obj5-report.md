# Objective 5 完成报告：元数据与变更接口

## 完成事项
- 扩展 `grh::ir::GraphView`：新增端口、符号、属性与调试信息访问，补齐 `valueSigned/valueIsInput/valueIsOutput` 等只读查询。
- 扩展 `grh::ir::GraphBuilder`：新增符号/端口/属性/调试信息写入接口，并补齐 `replace*`、`erase*`、`replaceAllUses` 等修改删除 API。
- `freeze()` 引入标记压实与 remap，清理已删除实体并生成连续布局；端口按名称排序输出。
- 测试：`tests/grh/test_grh.cpp` 增加符号迁移、端口排序、属性读取、SrcLoc 读写，以及 replace/erase 行为的覆盖用例。

## 构建与状态
- 已完成 `cmake --build build -j$(nproc)`。
- 已运行 `ctest --test-dir build --output-on-failure`，19/19 通过。

## 后续建议
- 将 GraphView/GraphBuilder 的新接口接入 elaborate/emit 热路径（Objective 6/7）。
- 根据后续需求补充属性索引优化与端口/符号冲突检查策略。
