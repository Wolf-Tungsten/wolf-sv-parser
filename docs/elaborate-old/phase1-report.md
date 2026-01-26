# GRH 阶段1 完成报告

## Objective 进展
- 已依据 `docs/GRH-representation.md` 落实 GRH 核心数据结构，并在 `include/grh.hpp` / `src/grh.cpp` 中提供序列化所需的操作与一致性校验。
- `grh-tests` 覆盖节点、连线及属性的主要行为，同时验证 JSON round-trip 与 artifacts 输出符合阶段目标。

## KR1 设计评审
- 产出：《`docs/elaborate/phase1-design-review.md`》，对比 `docs/reference/yosys` 与 `docs/reference/slang-netlist`，确认无需修改规范。
- 评审结论明确了 Operation/Value SSA 约束、属性域及端口同步策略，作为后续实现依据。

## KR2 核心结构与单测
- 数据结构实现：`include/grh.hpp` 与 `src/grh.cpp` 中提供 `Netlist/Graph/Operation/Value` 完整建模，新增 `replaceOperand/replaceResult` 等辅助接口，保持 SSA 与 usage 关系一致。
- 单测覆盖：`tests/grh/test_grh.cpp` 对重复符号、位宽、操作数/结果替换、属性限制进行校验，确保与规范对齐。
- 内存布局采用 `std::vector<std::unique_ptr<...>>` 管理，符合阶段规划。

## KR3 JSON 序列化与校验
- 序列化能力：`EmitJSON` 支持 JSON 输出（compact/pretty/prettyCompact）并保持与 `Netlist::fromJsonString` 的 round-trip。
- 属性约束：实现 `attributeValueIsJsonSerializable`，禁止嵌套数组、NaN/Inf 等非 JSON 合法类型（参见 `src/grh.cpp` 与对应单测）。
- 测试产物：`grh-tests` 运行时生成 `${CMAKE_BINARY_DIR}/artifacts/grh_stage1_demo.json`，供评审人工检视，位置已在 `CMakeLists.txt` 中通过 `GRH_STAGE1_JSON_PATH` 定义。

## 后续注意事项
- 保持 JSON schema 稳定，如需调整 `docs/GRH-representation.md` 必须先走审批流程。
- Elaborate 阶段应直接复用现有 API，避免绕过属性与 SSA 校验逻辑。***
