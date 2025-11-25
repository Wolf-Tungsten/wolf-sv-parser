# GRH 表示开发 OKR


# Objective 1

> 目标：彻底用 symbol 索引 Graph/Value/Operation，移除所有裸指针与旧命名，不保留向后兼容路径。

## 原则与顺序
- 统一 symbol 规范（命名规则、唯一性、序列化字段），在 `docs/GRH-representation.md` 与头文件注释落地。
- 依赖顺序：Graph 命名重命名 → Netlist 容器与 API 重写 → Graph 内部 Value/Operation 存储重写 → Value/Operation 关联关系纯 symbol 化 → 全量测试。
- 不留旧字段/别名/兼容 JSON；改动一次到位，保证实现与文档同步。

## KR1：Graph 的 moduleName 直接替换为 symbol
- 字段与接口：Graph 仅保留 `symbol_` 成员与 `symbol()` 访问器；构造函数参数改为 `symbol`，删除 name/moduleName 相关字段与别名。
- 使用点清理：更新 `include/grh.hpp`、`src/grh.cpp`、elaboration/emit 流程、测试用例和 `docs/GRH-representation.md`、`docs/grh/okr.md`；所有调用改用 `graph.symbol()`。
- 序列化：JSON 仅使用 `symbol` 字段，移除 moduleName fallback；旧格式不再支持。

## KR2：Netlist 以 symbol 管理 Graph（无裸指针）
- 容器形态：`graphs_` 仅存 `std::unordered_map<std::string, std::unique_ptr<Graph>>`，对外不暴露指针。
- API：`createGraph(symbol)` 校验唯一性并返回 `Graph&`；`findGraph(symbol)` 返回可空引用包装或迭代器；alias 映射调整为 `unordered_map<symbol, symbol>`；`topGraphs_` 存 symbol 列表。
- 生命周期：移除 `graphByName_`、`graphAliasByName_` 裸指针缓存；移动/拷贝语义在重写后确保 Graph.owner 重新绑定。

## KR3：Graph 内部的 Value/Operation 存储按 symbol 索引
- 句柄：定义 `ValueId/OperationId = std::string`（后续可替换为强类型）；Graph 内部用 `unordered_map<ValueId, size_t>`/`unordered_map<OperationId, size_t>` 映射到 `values_`/`operations_` 的 `unique_ptr` 下标。
- API：`createValue/createOperation` 只接受 symbol 与属性，内部注册句柄并返回引用；`find*` 返回可空引用包装；遍历接口提供 const view（不暴露裸指针），需要可变访问时通过句柄解引用。
- 端口绑定：`inputPorts_/outputPorts_` 存 ValueId；绑定/查询/序列化均走 symbol。

## KR4：Value 与 Operation 关联关系完全 symbol 化
- 数据成员：Value 的 `definingOp`、`users` 全部改为 `OperationId + operandIndex`；Operation 的 `operands/results` 存 `ValueId` 列表。
- 辅助访问：Graph 负责通过句柄解引用并维护一致性（添加/替换/移除时同步更新 users）；在接口层仅暴露 const 引用或句柄，不再暴露裸指针。
- 行为调整：`addOperand/addResult/replaceOperand/replaceResult` 接收引用/句柄后即转换为 symbol 存储；JSON 序列化/反序列化基于 symbol 写关系。

## KR5：验证与收敛
- 单测与断言：新增覆盖 symbol 查找、alias 解析、端口绑定、用户列表更新、移动语义下 owner 重绑的测试；引入断言防重复 symbol、悬空引用。
- 回归：完成重写后运行 `ctest`（或 `ninja test`）确保行为一致；必要时新增 JSON roundtrip 测试确认仅有 symbol 字段。

# Objective 2

> 目标：在 GRH 表示中引入电平敏感 latch（含可选异步复位），贯通文档、数据结构、elaborate、emit 与测试，从原先“报 latch 错误”转变为“生成 latch + warning”。

## 原则与顺序
- 语义对齐 `docs/grh/dlatch_support_request.md`/Yosys `$dlatch`、`$adlatch`：统一使用 `enLevel`/`rstPolarity` 字段，resetValue 宽度与数据一致，禁止零宽或多比特使能/复位。
- 仅在明确 latch 语境生成：`always_latch` 或组合写入覆盖缺失时产出 latch，同时保留 warning 诊断；其他流程（寄存器/内存）保持不变。
- 文档、JSON schema 与 emit 一次性补齐，属性/操作数校验与寄存器族一致，不留 TODO。

## KR1：需求与约束梳理
- 将 `docs/grh/dlatch_support_request.md` 的约束写清：`kLatch(en, d) -> q`，`enLevel=high/low`；`kLatchArst(en, rst, resetValue, d) -> q`，`rstPolarity=high/low`，`resetValue` 与 `d/q` 同宽。
- 明确 width 来源：沿用 Value 的位宽，不新增额外属性；en/rst 必须 1 bit，缺失或位宽不符时报错。
- 与寄存器区别：无 clk，透明窗口由 en 决定；symbol 仍需合法 Verilog 标识符。

## KR2：文档/表示层扩展
- 在 `docs/GRH-representation.md` 补充 kLatch/kLatchArst 小节，列出 operands/attributes/result、生成语义（参考 `$dlatch`/`$adlatch`），并更新操作列表与 JSON 字段说明。
- 若 `docs/overview.md` 或其他原语索引有列举，追加 latch 条目，保持属性命名一致。

## KR3：grh.hpp/cpp/序列化支持
- 扩展 `OperationKind`、`kOperationNames`、`toString`、`parseOperationKind`，让 latch 可被创建、dump、解析；新增属性校验：`enLevel`/`rstPolarity` 缺失或非法时报错，`resetValue` 必须存在于 kLatchArst。
- 若有操作数/结果数量检查，补充 latch 路径，确保 roundtrip/JSON dump 无歧义字段。

## KR4：elaborate 生成 latch（含告警）
- 组合流程的覆盖检查命中潜在 latch 时，不再终止：发出 warning（带位宽/位置），同时创建 kLatch/kLatchArst。enable 取当前 guard，data 取分支 RHS；存在显式异步复位时转 kLatchArst 并绑定 resetValue。
- `always_latch` 或显式 latch 模式复用同一构造路径；多个分支写同一目标时先合成 mux 得到 data，再接入 latch；未提供有效 enable 时给出错误而非静默生成。

## KR5：emit 支持 latch
- 为 kLatch/kLatchArst 生成 `always_latch`（或等效 `always @*`）块：`enLevel` 高/低，`rstPolarity` 高/低 + `resetValue`，保持 reg 声明/assign 与现有顺序一致。若缺失属性/操作数，输出清晰报错。
- 若 sequential block 分组逻辑需要，添加 latch 的 key/归类，避免与寄存器排序冲突。

## KR6：测试与回归
- 新增 Graph/JSON roundtrip/emit 用例：kLatch（en 高/低）、kLatchArst（rst 高/低 + resetValue）、elaboration latch 推断（缺失 else、`always_latch`）。检查 warning 文本与定位。
- 更新现有“防 latch”负例预期：从报错改为 warning + latch 节点；回归 `ctest` 或相关目标，确认寄存器/内存路径未被破坏。
