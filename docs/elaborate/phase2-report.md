# Elaborate 阶段2 完成报告

## Objective 进展
- 完成从 slang AST 到 GRH Netlist 的最小可行链路，实现端口信息同步与占位节点生成。
- 新增 `--dump-grh` 命令行选项，可以在编译结束后输出 GRH JSON，便于人工抽查。

## KR1 slang 前端加载管线
- 复用 `slang::driver::Driver` 构建前端流程，保持与 CLI 一致的选项处理，并在 `tests/elaborate/test_elaborate_smoke.cpp` 中走通从源文件到 `Compilation` 的管线。
- `Elaborate` 构造函数接受 `ElaborateDiagnostics`，在转换过程中可以直接关联 AST 上下文，后续阶段可继续复用该入口。

## KR2 elaborate 管线骨架
- 在 `Elaborate::convert` 中针对顶层模块创建 GRH Graph，生成输入/输出端口对应的 SSA Value 并标记顶层图。
- 为模块体添加 `kBlackbox` 占位 Operation，记录模块名称与“TODO”状态，JSON 导出时可见。
- CLI 新增 `--dump-grh`，调用 `EmitJSON` 生成 JSON（默认 prettyCompact），手动评审时可与占位节点一并查看。

## KR3 诊断与 smoke 测试
- 新增 `ElaborateDiagnostics`，支持 `TODO` / `NYI` 分类及源符号路径；CLI 会打印对应信息，并尝试解析文件位置。
- 对未支持的端口（匿名、InOut、接口端口等）统一发出 `NYI` 告警，同时保留占位 Value 便于后续实现。
- 引入 `ctest` 目标 `elaborate-smoke`，对 `testcase/t0/t0.sv` 运行前端+Elaborate 全流程，断言端口、占位节点与 `TODO` 诊断均存在。

## 后续注意事项
- 当前 Graph 名称基于定义名，尚未处理参数化实例重名问题；阶段3完成层级建模时需要补充唯一性策略。
- 端口解析暂只针对固定宽度的 bitstream 类型，其余类型以 `NYI` 告警兜底，后续阶段需要补齐结构体/数组的展开逻辑。
- `Elaborate` 仅为顶层图生成占位节点，后续阶段扩展时需要在遍历过程中复用同一诊断与 JSON 输出流程，避免重复告警。
