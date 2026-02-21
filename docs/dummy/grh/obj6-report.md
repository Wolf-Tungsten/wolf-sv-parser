# Objective 6 完成报告：GraphView 反向构建闭环

## 完成事项
- 新增 `wolvrix::lib::grh::GraphBuilder::fromView`，可从 `GraphView` 重建构建器并保持 `GraphId` 一致性。
- 覆盖 ports/attrs/srcLoc/symbol/useList/def-use 的拷贝与一致性校验，异常时给出明确错误。
- 新增专门的 round-trip 测试 `tests/grh/test_grh_roundtrip.cpp`，验证 GraphView → Builder → freeze 的结构与元数据一致性。

## 构建与状态
- 已完成 `cmake --build build -j$(nproc)`。
- 已运行 `ctest --test-dir build --output-on-failure -R grh-roundtrip-tests`，1/1 通过。

## 后续建议
- 在 elaborate/emit 迁移开始时，复用 `fromView` 进行阶段性一致性校验（Objective 7/8）。
