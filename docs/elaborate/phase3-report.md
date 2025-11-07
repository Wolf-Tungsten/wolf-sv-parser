# Elaborate 阶段3 完成报告

## Objective 进展
- 完成模块层次结构建模，在 Elaborate 流水线中根据 slang AST 构建层次化 GRH 图。
- 支持结构体、数组等复合端口类型的扁平化与位宽推导，所有节点/边均符合 `docs/GRH-representation.md` 规范。
- 在顶层 `Netlist` 中生成层次关系：每个模块体对应独立 Graph，实例化关系以 `kInstance` Operation 表示，并挂接端口名称元数据，便于后续连线。

## KR1 模块声明与端口解析
- 在 `Elaborate::materializeGraph` 建立定义体到 Graph 的映射，确保同一模块体只被构建一次，并对匿名定义合成稳定名称，避免重复建图。
- `populatePorts` 中使用 TypeHelper 分析 slang Type，生成 GRH Value 并记录 `isInput`/`isOutput`，同时将 Port 与内部符号写入缓存，保证实例化时可复用。

## KR2 TypeHelper 扁平化支持
- `TypeHelper::analyze` 可对 packed struct、固定长度 packed/unpacked array 进行位段展开，计算扁平位宽并追踪字段层级，后续阶段可以在此基础上继续扩展 slice/concat 逻辑。
- 对非法或零宽场景发出 `NYI` 诊断，保持与 stage2 一致的容错。

## KR3 层次化 GRH 与 kInstance
- `convertInstanceBody` 递归遍历模块体，跳过非模块实例，同时处理 `InstanceArraySymbol`，为每个元素生成 `kInstance`。
- `createInstanceOperation` 将实例的输入/输出端口绑定为 operands/results，添加 `moduleName`、`instanceName`、`inputPortName`、`outputPortName` 属性；输出端口对应的 Value 现在会记录 `defineOp`，能够体现驱动关系。
- 占位 `kBlackbox` 操作仍然存在，但已在模块末尾统一下发，并对每个模块体记录 TODO 诊断以提示后续实现。

## KR4 回归样例与诊断
- 新增 `tests/elaborate/data`：
  - `hierarchy_nested.sv`：三层嵌套实例；
  - `param_instance.sv`：参数化实例场景；
  - `struct_array.sv`：端口为 packed struct 与多维数组，验证 TypeHelper。
- `tests/elaborate/test_elaborate_hierarchy.cpp`：
  - 复用 slang CLI 构建 Compilation，调用 Elaborate 得到 Netlist；
  - 针对三种场景检查 Graph/Operation/Value 结构与属性；
  - 将 Netlist JSON 以 pretty 格式写入 `build/artifacts/elaborate_stage3/{nested,param,struct}.json` 供人工对照。
- CTest 新增 `elaborate-hierarchy` 目标，确保阶段3功能在 CI 中持续校验。

## 后续注意事项
- 当前值缓存 `valueCache_` 在一次 Elaborate 调用内全局共享，后续若引入并行或多根遍历需考虑作用域清理。
- 端口扁平化仅覆盖固定尺寸类型，动态数组/接口端口仍会触发 `NYI`；下一阶段在处理参数化实例与 generate 时需补充。
- `kInstance` 结果 Value 尚未与内部 Graph 端口对齐校验，需在后续 elaboration 阶段结合 assign/builders 进一步检查连线完整性。*** End Patch
