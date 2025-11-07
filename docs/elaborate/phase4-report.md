# Elaborate 阶段4 完成报告

## Objective 进展
- Elaborate 现已针对参数化模块生成独立 Graph，并复用 slang 的 canonical body 映射来避免相同实例重复建图。
- 引入 generate 语句与实例数组的遍历逻辑，确保循环/条件生成出来的实例同样参与 GRH 层次结构。
- 调整 Graph 命名策略：新增参数后缀（例如 `pg_leaf$WIDTH_4`），同时去除 `=` 等无法用于 Verilog 模块名的字符，便于后续反向生成/映射。

## KR1 参数化模块独立建图
- `Elaborate::materializeGraph` 增加 canonical body 缓存：对语义等价的实例复用 Graph，对参数化后产生新体的实例则重新建图，满足“独特模块独立建图、非参数化模块复用”的要求。
- 通过 `deriveParameterSuffix` 将覆盖后的参数值编码进 Graph 名称，并在 `sanitizeForGraphName` 中限制字符集与首字符规则，保证可读性与合法性。
- 更新 smoke/hierarchy 测试的断言逻辑，允许顶层 Graph 名包含参数后缀并校验特定实例引用了正确的后缀名称。

## KR2 generate 与实例数组支持
- 新增 `processGenerateBlock`、`processGenerateBlockArray`、`processInstance` 等助手，遍历 `GenerateBlockSymbol` / `GenerateBlockArraySymbol`，递归展开 generate for/if 等构造中出现的实例。
- 在 `processInstanceArray` 中统一复用 `processInstance`，并对嵌套数组、generate 块进行深度处理，确保复杂层次都能生成 kInstance 关系。
- 维持原有诊断策略，对未支持的符号类型继续发出 `NYI`；已处理的 generate/数组实例会完整连接端口，并附带端口名属性供后续检查。

## KR3 回归样例与 artifacts
- 新增 `tests/data/elaborate/param_generate.sv`，覆盖：参数共享 + 参数偏离的实例、generate for 循环、实例数组等组合场景。
- `tests/elaborate/test_elaborate_hierarchy.cpp` 增加 `validateParamGenerate`，统计 Graph/实例数量并检查不同参数化实例指向相应的 Graph。原有测试更新以适配新的命名规则。
- CMake 统一测试输入目录为 `tests/data/elaborate`，输出 JSON 工件集中在 `build/artifacts/elaborate/{nested,param,param_generate,struct}.json`，与阶段目标一致。

## 后续注意事项
- 当前参数值序列化依赖 `ConstantValue::toString`，对大宽度或复合类型需进一步评估是否需要自定义格式。
- generate 块中除实例外的语句（例如连续赋值、过程块）仍待后续阶段支持；现阶段会保留 `TODO` 占位。
- Graph 命名中的 `$` 与 `_` 与 Verilog 兼容，但需在后续生成代码时保持一致的映射策略，避免再次叠加重命名规则。
