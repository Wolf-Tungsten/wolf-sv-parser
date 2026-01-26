# feat001 inout support plan

## 目标
- 以 3-value 模型表达 inout 信号，保证 GRH SSA 不被破坏。
- kInstance/kBlackbox 新增 inout 端口元信息，并在 emit 时合并为单一 inout 端口连接。
- 统一命名规则，便于解析、emit、诊断和调试。

## 文档先行
- 任何 inout 相关代码改动必须先更新文档，再进入实现。
- 需要同步更新的现有文档：
  - `docs/GRH-representation.md`（端口模型、DPI 语义）
  - `docs/elaborate/feat001-inout-support.md`（本方案与进度）
  - 视实现情况更新 `docs/elaborate/memo.md` 或测试说明

## 非目标
- 不改变现有 non-inout 端口语义与 JSON 结构。
- 不引入新的 DPI 任务/上下文/pure 等特性，仍保持现有限制。

## 3-value 模型与命名规则
- 每个 inout 端口 `BASE` 对应三个 Value：
  - `BASE__in`  : 读值（从 pad 读入）
  - `BASE__out` : 驱动值（写到 pad）
  - `BASE__oe`  : 驱动使能（1=驱动，0=高阻）
- 三者必须共享相同的 `BASE` 前缀，后缀固定为 `__in/__out/__oe`。
- 所有 inout 相关 Value 由统一 helper 生成，避免出现手写不一致的 suffix。

## GRH/IR 表达调整
### Graph 端口
- 为 Graph 新增显式 inout 端口分组结构（推荐），避免仅靠命名推断：
  - `struct InoutPort { SymbolId name; ValueId in; ValueId out; ValueId oe; }`
  - GraphBuilder/GraphView/Graph 增加 `bindInoutPort` 与 `inoutPorts()`。
  - JSON 写入/读取新增 `ports.inout` 数组，元素包含 `name/in/out/oe`。
  - `grh_verify` 对 inout 端口进行校验（宽度一致、value 属于同一 Graph）。
- 若暂不改 JSON，可退化为仅在 emit 阶段按命名规则合并，但需要明确风险：难以区分普通 signal 与 inout triple。

### kInstance/kBlackbox 端口协议
- 新增属性 `inoutPortName`（vector<string>），记录 inout 端口名列表，顺序与端口声明一致。
- 操作数/结果顺序约定（append 到末尾）：
  - operands: `inputs...`, 然后对每个 inout 追加 `out`, `oe`
  - results : `outputs...`, 然后对每个 inout 追加 `in`
- 三个 value 对应同一 `BASE`，命名与 3-value 规则一致。

### DPI import/call
- `kDpicImport` 的 `argsDirection` 允许 `inout`。
- `kDpicCall` 新增 `inoutArgName`（vector<string>），记录 inout 形参名列表。
- 操作数/结果顺序约定（append 到末尾）：
  - operands: `clk`, `enable`, `in args...`, `inout args...`
  - results : `out args...`, `inout args...`
- 约束：inout 形参必须连接到可写 LHS（net/reg），且位宽固定。

### JSON 扩展
#### Graph.ports
- 现有 `ports.in` / `ports.out` 保持不变。
- 新增 `ports.inout` 数组，元素为对象：
  - `name`: 端口名
  - `in`: 读值的 Value symbol（`BASE__in`）
  - `out`: 驱动值的 Value symbol（`BASE__out`）
  - `oe`: 驱动使能的 Value symbol（`BASE__oe`）
- inout 三个 Value 在 `vals` 中的 `in/out` 标志保持 `false`，`inout` 标志设置为 `true`（input/output 仅由 `ports.in/out` 指定）。

示例：
```json
{
  "ports": {
    "in": [{ "name": "i_clk", "val": "i_clk" }],
    "out": [{ "name": "o_led", "val": "o_led" }],
    "inout": [
      { "name": "pad", "in": "pad__in", "out": "pad__out", "oe": "pad__oe" }
    ]
  }
}
```

#### kInstance / kBlackbox attributes
- 新增 `inoutPortName`，其 JSON 表达为 `vector<string>`。
- operands/results 顺序不变，仅在末尾追加 inout 对应 Value。

#### DPI inout
- `kDpicImport.argsDirection` 允许 `"inout"`。
- `kDpicCall` 新增 `inoutArgName`，JSON 表达为 `vector<string>`。
- operands/results 与文档中约定顺序一致，不在 JSON 中额外记录偏移。

## Elaborate 调整
1) 端口解析（populatePorts）
   - 对 `ArgumentDirection::InOut` 创建 `BASE__in/__out/__oe` 三个 Value。
   - 通过 `bindInoutPort`（或临时表）记录端口名与三 Value 的映射。
   - 将端口符号读值映射为 `BASE__in`。

2) LHS/RHS 行为
   - RHS 使用 `BASE__in`。
   - LHS 写 inout 时必须明确驱动值与 oe，建议仅支持以下模式：
     - `assign BASE = oe ? out : 'bz;`
     - `assign BASE = oe ? out : {W{1'bz}};`
   - 解析上述模式时，写入 `BASE__out` 与 `BASE__oe`；否则 NYI 诊断。

3) 实例化与 blackbox 连接
   - 扩展 instance 端口收集逻辑，识别 inout 方向。
   - 追加 `inoutPortName` 属性并按顺序追加 operands/results。
   - 对缺失连接或宽度不匹配给出清晰诊断。

4) DPI inout
   - `collectDpiImports` 接受 `ArgumentDirection::InOut`，记录为 `inout`。
   - `materializeDpiImports` 写入 `argsDirection` 允许 `inout`。
   - `kDpicCall` 生成时：
     - inout 形参既要绑定输入 operand，也要生成输出 result。
     - 通过 write-back 将输出 result 写回对应的 LHS target。
   - 诊断规则：inout 实参不是 LHS、位宽未知或不匹配时明确 NYI。

## Emit 调整
1) 模块端口声明
   - 对 Graph 的 inout 端口生成 `inout` 声明。
   - 生成桥接逻辑：
     - `assign BASE__in = BASE;`
     - `assign BASE = BASE__oe ? BASE__out : {W{1'bz}};`

2) kInstance/kBlackbox 连接
   - 读取 `inoutPortName`，按顺序从 operands/results 获取 out/oe/in。
   - 为每个 inout 端口生成局部 wire（推荐 `__inout_${inst}_${port}`）。
   - 连接方式：
     - `assign wire = oe ? out : {W{1'bz}};`
     - `assign in = wire;`
     - 实例端口 `.PORT(wire)`

3) 端口合并规则
   - inout 端口只允许一个 wire 参与连接，避免 SSA 多驱动。
   - 发现多重驱动/多重 read 但缺失 oe 时，明确 NYI。

4) DPI inout
   - `kDpicImport` 中 inout 参数直接输出 `inout` 方向。
   - `kDpicCall` 中为 inout/out 形参生成临时变量：
     - inout: `tmp = in_value; dpi_call(... tmp ...);`，并保持 `assign out_value = tmp;`
     - out: 维持现有 `tmp` + `assign out_value = tmp;` 策略
   - inout 参数名需与 `inoutArgName` 对齐，避免与 out arg 混用。

## 测试计划
- 新增 `tests/data/elaborate/inout.sv`：
  - 基础 inout 端口 + tri-state assign 模式。
  - 模块 A<->B inout 级联连接（kInstance inout wiring）。
  - 负例：非 tri-state 赋值（应 NYI）。
- 新增 `tests/elaborate/test_elaborate_inout.cpp`：
  - 校验 inout 端口三 value 命名、kInstance operands/results 顺序、`inoutPortName` 属性。
  - JSON artifact 输出校验。
- 扩展 `tests/data/elaborate/dpic.sv`：
  - 新增 `dpic_inout_basic`（inout 参数读写）。
  - 负例：inout 实参为非 LHS（应 NYI）。
- 扩展 `tests/elaborate/test_elaborate_dpic.cpp`：
  - 校验 `kDpicImport` argsDirection 包含 `inout`。
  - 校验 `kDpicCall` 的 `inoutArgName`、operands/results 顺序，以及 write-back 目标。

## 实施步骤
1) 设计与基础结构
   - 在 `include/grh.hpp` / `src/grh.cpp` 加入 inout 端口表与 JSON 支持。
   - 同步更新 `docs/GRH-representation.md` 与 `docs/elaborate/memo.md`（如需）。
2) Elaborate 接入
   - `src/elaborate.cpp` 允许 `ArgumentDirection::InOut`，创建 3-value 并注册映射。
   - LHS/RHS 转换器支持 tri-state 赋值识别与 `__out/__oe` 写回。
3) Instance/Blackbox 端口与属性
   - `kInstance/kBlackbox` 增加 `inoutPortName`，调整 operands/results 排序。
4) DPI inout
   - `kDpicImport` / `kDpicCall` 支持 inout 方向，写回路径接入 write-back。
5) Emit 合并逻辑
   - `src/emit.cpp` 支持模块端口 `inout` 输出与 instance 端口合并。
6) 测试与回归
   - 添加 inout 专用测试并跑 `ctest -R elaborate-inout`（或纳入全量）。
