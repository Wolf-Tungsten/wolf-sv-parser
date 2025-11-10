# 阶段12：CombAlwaysConverter 实现情况

## 完成内容
- 在 `Elaborate::processCombAlways` 中接入新的 `CombAlwaysConverter`，可为每个 `always_comb` / `always @(*)` 块创建局部 shadow memo，并在块结束后统一写入 `WriteBackMemo`（src/elaborate.cpp）。
- `CombAlwaysConverter` 引入专属的 `CombAlwaysRHSConverter`、`CombAlwaysLHSConverter`，阻塞赋值解析时优先读取 shadow value，保证 `a = 1; b = a;` 语义正确（include/elaborate.hpp, src/elaborate.cpp）。
- 提炼原 `ContinuousAssignConverter` 的 LHS 逻辑为 `LHSConverter` 基类，并派生 `ContinuousAssignLHSConverter` 与 `CombAlwaysLHSConverter`，两条管线复用同一份切片/路径解析实现（include/elaborate.hpp, src/elaborate.cpp）。
- 新增 `tests/data/elaborate/comb_always.sv` 与 `tests/elaborate/test_elaborate_comb_always.cpp`，覆盖 block 内多次阻塞赋值与 `always @(*)` 的 OR 逻辑；`CMakeLists.txt` 中注册对应的测试目标和 JSON artifact。

## 测试记录
- `cd build && ctest`：全部 8 个单元/集成测试通过，包含新增的 `elaborate-comb-always`。***
