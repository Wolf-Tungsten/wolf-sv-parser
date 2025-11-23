# Emit 输出 OKR

> 每次开始工作前，回顾 docs/overview.md 和 docs/GRH-representation.md  
> 每次开始工作前，回顾前序阶段的完成情况（若有 docs/emit/phase*-report.md 则需阅读）  
> 本阶段目标：为 Emit 基类搭建可落地的框架，后续 JSON/Verilog 输出在此基础上派生

## 阶段1：搭建 Emit 类框架
- **目标定位** 搭建 `Emit` 抽象层，统一 GRH 到外部表示的输出入口，确保 JSON/Verilog 等具体实现只需继承并复用公共校验、诊断、输出流管理逻辑。
- **KR1 对齐：文件与构建骨架**
  - 新增 `include/emit.hpp`、`src/emit.cpp`，定义命名空间、前置声明、基础类型，确保编译目标被纳入现有 CMake/测试编译流程。
  - 约定对外接口（如 `Emit::emit(const grh::Netlist&, const EmitOptions&)`）在头文件声明、源文件提供最小默认实现或纯虚钩子，保证后续派生类落地时无需调整主入口。
  - 记录 TODO/设计假设（输出目录策略、日志接口对接位置）以便阶段评审。
- **KR2 对齐：基类职责与上下文设计**
  - 设计 `EmitOptions` / `EmitResult` / `EmitDiagnostics` 等轻量结构，覆盖输出路径、是否 pretty-print、目标顶层列表 override 等通用开关，并提供可扩展的 key-value 属性占位。
  - `Emit` 基类需包含：输入 `grh::Netlist` 的只读视图、诊断收集器、文件/流打开辅助函数、顶层存在性与端口合法性基础校验；对派生类暴露受保护的 `emitImpl` 纯虚接口。
  - 规划派生类的责任边界（JSON/Verilog 仅负责序列化逻辑，公用校验/资源管理留在基类），并在注释中固定期望的生命周期/线程安全假设。
- **KR3 对齐：测试/样例目录准备**
  - 创建 `tests/data/emit` 目录，准备最小可重用的 GRH Netlist JSON 或构造辅助（可先留空但添加 README 指明用途与格式约定）。
  - 创建 `tests/emit` 目录，规划单元测试用例：1）无顶层/空网表的错误路径；2）简单单模块网表的通过路径；3）诊断聚合与返回值的基类行为。必要时为后续派生类保留基类测试夹具。
  - 将测试骨架纳入 ctest（新增目标或占位测试文件），确保阶段收尾可运行并作为后续实现的回归入口。
- **验收口径**
  - CMake/编译配置中能找到新文件，编译通过且不破坏现有管线。
  - 头文件声明的入口和选项结构稳定清晰，具备可扩展性和最小诊断能力。
  - 测试目录与占位样例就绪，至少包含针对基类框架的空跑/失败路径测试计划或骨架。

## 阶段2：将 JSON 生成迁移到 EmitJSON 中
- **目标定位** 把目前散落在 GRH/Elaborate 内的 JSON 序列化集中到 `EmitJSON` 派生类中，实现更轻量的输出格式与统一的入口，同时为 CLI/测试提供稳定的接口。
- **KR1 对齐：JSON 格式规范与文档**
  - 梳理现有 JSON 输出链路（`Graph::writeJson` 等），制定压缩命名规则（如 `ops`、`vals`、`attrs`、`in`/`out`、`kind` 简写），保持可读性的前提下减少冗余 key。
  - 约定排序与布局：模块按名称排序，端口按名称排序，Operation/Value 均按插入顺序；Value、Operation、Port等小结构单行展示，Operation/Graph 支持 pretty/compact 两种模式。
  - 在 `docs/GRH-JSON-spec.md` 编写规范：顶层结构、字段含义、类型约束、示例片段、与 `docs/GRH-representation.md` 的对应关系以及兼容性/未来扩展预留。
- **KR2 对齐：EmitJSON 实现与集成**
  - 新增 `EmitJSON`（头在emit.hpp/源文件在emit.cpp，命名空间 emit），继承基类并实现 `emitImpl`：接受 `Netlist` 和顶层列表，输出到指定目录（默认 `grh.json`），支持 `jsonMode` 以切换 compact/prettyCompact/pretty。
  - 将历史的 `toJsonString` 入口迁移/薄包装到 `EmitJSON`，避免网表类直接持有序列化逻辑，减少重复实现。
  - 更新 CLI `--dump-grh` 和内部调用路径，改用 `EmitJSON`，确保诊断透传（如缺少 top 时提示）并保持现有行为兼容。
- **KR3 对齐：测试与回归覆盖**
  - 在 `tests/emit` 增加针对 `EmitJSON` 的单元测试：1）空/无顶层报错；2）单模块最小网表输出、校验关键字段与 key 简写；3）pretty/compact 差异。
  - 回归现有依赖 JSON 的测试（例如 `tests/grh/test_grh.cpp` 写文件路径、elaborate 阶段 JSON dump），迁移到新接口或更新期望输出，确保 ctest 全通过。
  - 在 `tests/data/emit` 添加示例网表与期望 JSON 片段（可简短），用于格式断言；确保 CMake 将 EmitJSON 测试纳入默认测试集。
- **验收口径**
  - `docs/GRH-JSON-spec.md` 完成并描述清晰的字段/示例与兼容策略。
  - `EmitJSON` 可在 CLI/测试中成功输出 JSON，缺失 top 等异常能返回诊断，格式满足压缩与排序约定。
  - 所有受影响测试更新并通过（ctest），不再依赖旧的 GRH 内建序列化路径或已与新实现一致。

## 阶段3：在 EmitJSON 中添加新模式
- **目标定位** 引入介于 pretty/compact 之间的 `prettyCompact`，在保持缩进与可读性的同时减少 JSON 行数，方便 diff 与人工检查。
- **KR1 对齐：Graph 子数组单行化**
  - 梳理现有 pretty 模式在 `vals`/`ports`/`ops` 的输出，抽象出数组元素渲染钩子，新增 `prettyCompact` 仅将这些数组的元素压缩为「每元素一行」的单行对象。
  - 为 Value/Port/Op 的渲染封装单元化函数或模板，避免第三种模式出现重复逻辑；确保简写字段保持与 KR1 规范一致。
- **KR2 对齐：缩进/换行保持**
  - 提取 Netlist/Graph 层级的缩进与换行控制（对象起止行、字段顺序、空格数量）为共享策略，`prettyCompact` 复用 pretty 的块级布局，仅局部调整数组元素换行策略。
  - 检查 CLI/EmitOptions 默认模式是否需要更新（如默认改为 prettyCompact），并对已有调用点进行行为确认，避免破坏 JSON 排版假设。
- **KR3 对齐：测试与期望更新**
  - 在 `tests/emit` 增加 `prettyCompact` 针对性快照/字符串断言，覆盖 Graph 内三个数组的单行化效果，以及与 pretty/compact 的差异。
  - 迁移现有 JSON 期望输出（含 elaborate 阶段产物）到 `prettyCompact`，并在 `docs/GRH-JSON-spec.md`/相关 README 追加新模式说明与示例。
  - 确认 ctest/CLI 跑通且输出路径一致，避免新增模式导致文件名或扩展变更。
- **验收口径**
  - EmitJSON 支持三种模式并通过选项可控，默认策略满足阶段要求（若切换默认则文档/调用同步）。
  - `prettyCompact` 输出满足：Netlist/Graph 缩进与 pretty 一致，`vals`/`ports`/`ops` 数组元素单行展示；无多余空行或缩进异常。
  - 所有依赖 JSON 的测试/样例更新为新模式并通过，CLI 输出与文档示例一致。

## 阶段4：实现 EmitSystemVerilog 类基本功能
- **目标定位** 提供首个可落地的 Verilog 兼容输出链路（除 DPI-C 外尽量避免 SystemVerilog 扩展），将 GRH Netlist 落盘为 `.sv`，并以 lint 级验证确保可被后续仿真/综合工具接受。
- **KR1 对齐：Netlist 到文件的生成管线**
  - 新增 `EmitSystemVerilog` 派生类，复用基类选项（输出目录、顶层覆盖、诊断收集），默认为每个 Netlist 生成单个 `<netlist>.sv`（内容保持 Verilog 2001 友好，仅 DPI-C 使用 SystemVerilog 语法），并按顶层优先、名称次序遍历 Graph 生成对应 module。
  - 设计信号/模块命名规则（端口/中间 Value/实例名），避免重名和 SystemVerilog 关键字冲突；保持跨段引用一致的去重表。
  - 在 CMake/CLI 入口处接线新输出模式（如 `--emit-sv`），确保与 JSON 流水线共存，默认输出目录/文件名可通过 EmitOptions 配置。
- **KR2 对齐：模块段落与运算映射**
  - 映射规则应尽量对齐 `docs/GRH-representation.md` 中定义的各类 Op 生成语义，缺口处用 TODO/诊断标注。
  - **模块定义（Port）**：从 Graph ports 生成 module 端口表，方向/位宽/有符号性依据 Value/Type 推导，端口排序与 JSON 规范一致；必要时为 `clock/reset` 等保留名称提供配置钩子。
  - **wire 定义区段（Value）**：为非端口的中间 Value 分配 `wire`/`reg`（依据驱动方式），区分单比特与向量，避免使用 `logic`/`typedef`/`struct` 等 SystemVerilog 扩展。
  - **reg/mem 定义区段（reg/memOp）**：收集时序寄存器与存储器操作的目标，生成 `reg`/数组定义，记录复位值/宽度/深度信息，确保在时序块中使用前已声明。
  - **实例化区段（kInstance/kBlackbox）**：将实例/黑盒操作转成 module 实例化语句，处理参数覆写、端口映射（命名关联优先），保留黑盒占位注释以便外部提供实现。
  - **DPIC 导入区段（kDpiCImport）**：输出 `import "DPI-C"` 原型，覆盖函数/任务、返回类型和参数列表，保持名称与后续调用一致。
  - **连续赋值区段**：将简单运算或直连映射为 `assign`，集中在声明之后便于阅读；若使用常量/切片需封装格式化助手。
  - **组合逻辑区段**：理论上不生成 `always @*` 组合块，尽量用 `assign` 完成纯组合逻辑；如确需过程块需显式标注原因并保持单驱动。
  - **时序逻辑区段**：将寄存器更新等时序操作收敛到 `always @(posedge clk ... )`（按需添加复位/使能），使用非阻塞赋值；相同时钟/复位特性的寄存器与 memport 写入尽量聚合进同一个 always 块，保持驱动与 reg 定义一一对应。
  - 区段顺序/空行格式固定，形成可预测布局；为尚未覆盖的 op 种类留出 TODO/诊断提示，避免静默忽略。
- **KR3 对齐：样例与 lint 验证**
  - 在 `tests/data/emit` 添加覆盖多段的最小网表示例（含端口/wire/reg/mem/实例/DPIC/组合/时序），并提供期望 `.sv` 片段或快照。
  - 在 `tests/emit` 编写单元测试：调用 `EmitSystemVerilog` 生成文件，与期望字符串/正则匹配；校验命名去重、段落顺序与必备语句存在。
  - 集成 `verilator --lint-only` 跑已生成的 `.sv`（通过 CTest/脚本驱动），如环境缺失则检测并跳过但提示；确保 lint 通过作为阶段验收。
  - 文档补充：在 `docs/emit` 或 README 记录使用方法、输出命名规则与 lint 流程，供后续阶段迭代参考。
