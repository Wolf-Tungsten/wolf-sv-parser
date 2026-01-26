# GRH 核心设计评审（阶段1 KR1）

| 关注点 | GRH（`docs/GRH-representation.md`） | Yosys RTLIL（`docs/reference/yosys`） | slang-netlist（`docs/reference/slang-netlist`） | 评审结论 |
| --- | --- | --- | --- | --- |
| 顶层组织 | Netlist → Graph → Operation/Value，模块对应 Graph，支持多顶层 | RTLIL::Design → Module → Wire/Cell，模块是主要容器 | Netlist::Design → ModuleGraph → Node/Edge，模块维度组织 | 保持 Graph 对应模块的结构；无需调整规范 |
| Operation 类型体系 | 预定义枚举涵盖常量/运算/层次/时序/DPI/调试 | Cell 类型使用字符串 + 参数区分；同类语义需查库 | NodeKind 枚举，但运算符分类更细；硬连线/行为共存 | GRH 枚举更精简，可通过 attributes 扩展；规范无需改动 |
| Operation 标识 | `symbol` 字段模块内唯一 | Wire/Cell 名称需在模块作用域唯一 | NodeId 自动编号，名称源于原 AST | 采用 symbol 方式可兼容，可通过工具链保持命名 |
| Operation 连接 | `operands`/`results` 以 Value 建模 SSA | Cell 使用 SigSpec 连接，多驱动需额外 pass 规整 | Node 输入输出端口列表，直接引用 ValueId | SSA 约束由 Value 管理即可，不需修改规范 |
| Operation 属性 | `attributes`: `bool/int64/double/string` 及其数组 | Cell/Module 参数支持 RTLIL 常量表达式 | Node attr 支持 `uint64`, `logic` 等，序列化成 JSON | GRH 属性范围覆盖需要；特定类型可封装到 string JSON |
| Value 唯一性 | 每个 Value 在 Graph 内唯一 `symbol`，要求 >0 位宽，SSA | RTLIL::Wire/Const 需名称唯一但允许 none；多驱动可存在 | ValueId 唯一，名称可空，SSA 需检查 | 保持 Value 必须有有效 symbol，对齐序列化需要 |
| Value 端口标记 | `isInput`/`isOutput` 与 Graph 端口字典一致 | Wire 通过方向标记和模块端口表 | NodePort 元数据标出方向 | 设计合理，后续 elaboration 需同步 graph 端口表 |
| Graph 端口 | `inputPorts`/`outputPorts` 字典引用 Value | Module 端口列表 + Wire | ModuleGraph::Port 列表 | 端口映射和 SSA 结构兼容，无需调整 |
| 生命周期管理 | Graph 拥有 Operation/Value；Value 记录 `defineOp` 与使用者 | RTLIL 模块拥有 Cell/Wire；SigSpec 通过引用 | 节点/值由 Netlist Arena 管理 | 需要在实现中提供集中内存管理（arena 或 std::vector） |
| 序列化目标 | JSON 仅用于人工检查，需稳定 schema | Yosys RTLIL JSON backend (`write_json`) 结构类似 | slang-netlist 提供 JSON/flatbuffers | 参考两者的键命名，确保 schema 固定；无需改规范 |

结论：GRH 规范的节点、边类型和属性设计与参考资料一致，不需要修改 `docs/GRH-representation.md`。实现时关注 SSA 下的 Value 生命周期与端口同步、属性基本类型序列化即可。
