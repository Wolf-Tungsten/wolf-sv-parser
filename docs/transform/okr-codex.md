# Transform 基础设施 OKR


# Objective 1

> 目标：改进 GRH 相关数据结构中的引用关系，为快速检索做好准备，并在 transform/pass 内保持可复制、可序列化的金标准引用。

## 原则与顺序
- 以 symbol 为唯一权威标识，指针仅作缓存与便捷访问；序列化、拷贝、等价性判断都基于 symbol。
- 命名统一：符号字段统一为 `xxxSymbol`，指针缓存统一为 `xxxPtr`；迁移/克隆后集中重建指针，不留隐式悬垂。
- 施工顺序：补齐数据结构与内部 API → 升级构造/查找/遍历接口 → 更新调用点与测试，逐步验证性能与正确性。

## KR1：Operation 与 Value 互引及 Graph 引用同时保留 symbol 与指针
- Value 增加 `definingOpPtr`，`ValueUser` 记录 `opSymbol + opPtr + operandIndex`，Graph 访问器能返回两类引用以便 transform 快速遍历 def/users。
- Operation 维护 `operandsSymbols/resultsSymbols` 与 `operandsPtrs/resultsPtrs` 双轨；Graph 保持以 symbol 为 key 的存储，但提供内部 `resolve*Ptr` 钩子填充/校验缓存。
- Netlist/Graph 的 move/clone/JSON 导入结束后，统一调用重挂流程（如 `rehydratePointers()`），确保缓存与 symbol 对齐并清理悬垂指针。

## KR2：指针缓存作为加速路径，读取接口返回引用避免拷贝
- `operandValue`/`resultValue`/`operands()`/`results()` 等接口改为返回 `Value&`/`const Value&` 视图或引用，内部直接解引用缓存；缓存缺失时按 symbol 查找并回填。
- 为常用反向查询（users 列表、definingOp）提供基于指针缓存的便捷访问器，减少 transform 热点中的字符串构造与哈希查找。
- 调试场景添加轻量断言/检查，比较缓存指针与 symbol 反查结果，发现漂移时立即修正或抛出。

## KR3：设置 operand/result/user 的入口用引用，内部同步 symbol 与指针
- `add/replaceOperand`、`add/replaceResult`、`Value::setDefiningOp`、`Value::addUser/removeUser` 等接口统一接受 `Operation&`/`Value&`，内部同时写入 symbol 和指针缓存。
- Graph 的端口绑定、Netlist 的 alias/top 注册与 transform 构造路径使用引用风格，删除外部手工填 symbol 的调用点；仅 JSON 解析等特殊路径使用受限符号版私有辅助。
- 更新所有调用者（elaborate/emit/transform/tests），避免重复查找或字符串拷贝，确保关系只在一个入口维护。

## KR4：symbol 作为金标准，复制/序列化依赖 symbol 重建关系
- Graph/Netlist 拷贝或 transform 中克隆节点时只复制 symbol 与属性，指针缓存通过集中钩子重建；禁止裸 memcpy 导致悬垂。
- emit/transform/JSON dump/pretty 打印等输出路径以 symbol 生成关系，确保缓存缺失也能 roundtrip；必要时在拷贝路径上修复/补充 def 与 users。
- 补充测试：构造含多用户与替换操作的图，验证移动后缓存重建、def/users 与 operands/results 双向一致，`ctest` 回归通过且性能热点查找次数下降。

# Objective 2

> 目标：Transform 以独立 pass 的形式实现，实现支持 pass 的基本框架。

## 框架定位与拆分
- 在 `wolf_sv::transform` 命名空间新增公共头 `include/transform.hpp` 与实现 `src/transform.cpp`，所有与Pass相关的基础设施（基类）都在上述两个文件中实现，创建 src/pass 文件夹 和 include/pass 文件夹，存放具体pass的实现（子类）
- Pass 持有对 `grh::Netlist` 的可变引用，明确 transform 仅在 Elaborate → Emit 之间运行。
- Pass 以就地修改 Netlist/Graph 为原则，不直接处理 I/O；与 emit/elaborate 同样使用轻量诊断结构，主流程统一汇总/打印。
- 约定 pass id（短字符串）与可读 name/desc，id 用于 CLI/注册，desc 用于日志/诊断，便于后续扩展内置/自定义 pass。

## KR1：Pass 基类（含参数）
- 定义 `struct PassContext { grh::Netlist& netlist; PassDiagnostics& diags; bool verbose; }`，预留 `Graph* currentGraph`/`std::string_view entryName` 等可选字段以支撑图级遍历。
- Pass 配置直接通过子类构造函数传入（不再有统一 PassConfig），基类接口保持精简：`class Pass { virtual PassResult run(PassContext&) = 0; std::string id() const; std::string name() const; };`，`PassResult` 含 `bool changed`, `bool failed` 和可选 `std::vector<std::string> artifacts`，失败时通过 `diags` 记录错误原因。
- 诊断沿用 Elaborate/Emit 风格：`TransformDiagnostics`/`PassDiagnostics` 记录 `kind (Error/Warn)`、`message`、`context`，打印时带 `[pass-id]` 前缀，方便定位。

## KR2：PassManager 顺序注册与执行
- 新增 `class PassManager { PassPipelineOptions options; std::vector<PassEntry> pipeline; PassRegistry registry; };`，`PassEntry` 持有 `factory`、`id`，注册顺序即执行顺序。
- `registry` 支持按 id 创建 pass，便于主流程通过字符串管线（如 `constprop,inline`）构建；测试可注入 fake pass factory 验证执行顺序。
- `run(Netlist&, PassDiagnostics&)` 逐个执行，累积 `changed`；遇到 `failed` 或 diagnostics 中 error 时根据 `options.stopOnError` 决定立即终止或继续，统一返回 `TransformResult{success, changed}`。
- 提供 `addPass(std::unique_ptr<Pass>)` 以便直接推入定制 pass，`clear()` 用于重复跑同一 Netlist 的不同管线；`options.verbose` 控制每个 pass 的开始/结束日志。

## KR3：main 集成 Elaborate→Transform→Emit
- 在 `src/main.cpp` elaboration 成功后创建 `transform::PassManager`，从命令行构建管线后调用 `run(netlist, passDiags)`；若 `success == false` 或 diagnostics 有 error，则打印诊断并返回非零 exit code。
- CMake 将 `src/transform/*.cpp` 与 `include/transform/*.hpp` 编译入主二进制；新增最小单元测试覆盖管线顺序、参数绑定与错误短路，后续可用 fake pass 验证 changed/diagnostics 聚合。
- 日志格式与 emit/elaborate 对齐（前缀 `[transform]` + pass id），保持 JSON/SV 输出不受影响，若 pipeline 为空则完全复用当前输出路径。

# Objective 3

> 目标：构建 GRHVerifyPass，校验/修复 GRH 图结构合法性，保证后续 transform 基于一致的拓扑与元数据运行。

## 定位与策略
- Pass id `grh-verify`，默认在 elaboration 后、其他变换前运行；定位为结构验证/轻量修复，不更改语义或生成新节点。
- 仅依赖 Netlist/Graph 状态与 pass diagnostics，不触碰 I/O；支持 `verbose` 打印检查统计（节点数、修复数、失败数）。
- 检查顺序：先 schema（kind 与 operand/result/attr 的约束），再 symbol 存在性，最后指针缓存/用户列表一致性；可在致命错误后继续收集其余错误以便一次性暴露问题。

## KR1：创建 GRHVerifyPass 骨架
- 文件落在 `include/pass/grh_verify.hpp` 与 `src/pass/grh_verify.cpp`，通过 PassRegistry 注册 id/name/desc；构造函数参数控制是否自动修复指针缓存（默认开启）与 `stop_on_error`。
- PassContext 复用 diagnostics，增加轻量统计结构记录修复次数/错误计数；遍历入口覆盖所有 Graph/Operation/Value（含子图），保持拓扑遍历封装在私有辅助函数中。
- 失败条件：遇到不可修复的缺失 symbol/未知 kind 等直接标记 `failed`，并写入 diag；可修复项完成修复后仅计入 warn/info。

## KR2：Op 合法性校验
- 建立 `OperationSpec`（按 `OpKind`）描述 `expectedOperands`/`expectedResults`（固定或区间）与必需属性列表；若已有枚举/元数据结构则优先复用。
- 遍历 Operation 时校验 kind 合法性、操作数与结果数量匹配，缺失 attr 记为 Error，属性类型错误同样 Error；多余的 attr 仅做 Info 级提示，不阻断流程；属性类型检查（如整数/字符串/枚举范围）用专门校验器，避免散落在 pass 逻辑中。
- 对于 schema 违规的 op，输出携带 op symbol/kind 与期望/实际值的错误信息，帮助定位；保持统计以便 verbose 输出摘要。

## KR3：连接关系校验与修复
- 先按 symbol 检查 `operandsSymbols/resultsSymbols`、Value `definingOpSymbol`、user 记录的 `opSymbol` 是否能在所属 Graph/Netlist 中解析；缺失直接报错并视为不可修复。
- 对解析成功的实体比对指针缓存与 symbol 反查结果：不一致则在 `autoFixPointers` 开启时回填/纠正（`operandsPtrs/resultsPtrs/definingOpPtr` 等），并诊断一次修复；关闭自动修复则仅报 warn。
- 校验 Value 的 users 列表：验证用户 op 与 operandIndex 合法、去重，同步回写缺失/错误的 user 关系；必要时重建单个 Value 的 user 集合以消除漂移。
- 检查节点所属 Graph 一致性与循环引用（如 def 与 user 不在同一图、或 def 指向空），发现后报错并阻止后续 fix，避免破坏拓扑。

## 诊断与测试
- Diagnostics 分级：schema/缺失 symbol 记为 Error，自动修复记为 Warn/Info（含修复详情与节点标识），多余 attr 仅 Info；pass 结束时在 verbose 下打印统计摘要。
- 新增 transform 单测覆盖：1) kind/schema 违规报错；2) symbol 缺失导致失败；3) 指针缓存错误但可按 symbol 修复；4) user 列表缺项/错位被重建；5) 多余 attr 仅产生 Info 诊断；相关 fixture 写在 `tests/data/transform/grh_verify_*`。
