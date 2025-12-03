# 阶段2 完成报告：将 JSON 生成迁移到 EmitJSON

- **完成概览**：JSON 序列化集中到 `EmitJSON`，支持 compact/pretty/prettyCompact 三种模式，压缩键名（vals/ops/ports/tops/attrs）并维持端口/模块排序。
- **产出物**：
  - 代码：`EmitJSON::emitImpl` 与 `emitToString`，使用 `JsonPrintMode` 选择布局。
  - 文档/样例：`tests/data/emit/demo_expected.json` 作为格式参考。
  - 测试：`tests/emit/test_emit_json.cpp` 覆盖缺顶层失败、模式差异、round-trip 解析。
  - CLI：`--emit-json` 现基于 EmitJSON。
- **验证**：`cmake --build build && ctest` 通过（含 emit-json）。
- **遗留/风险**：尚未做大规模网表的性能/体积评估；attrs 的类型合法性依赖调用方保障。*** End Patch ***!
