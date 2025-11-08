# Elaborate 阶段6 完成报告

## Objective 进展
- 全面替换 Elaborate 中对「wire」的称呼，将组合类信号统一表述为「net」，并同步更新 `okr`/阶段文档，确保术语与 SystemVerilog 定义一致。
- TypeHelper 结果现在写入 net/reg memo：每个 `SignalMemoEntry` 都附带 flatten 后的字段列表，可直接映射到 GRH Value 的位段，在复杂结构体/数组场景下也能定位 slice。
- `collectSignalMemos` 已支持 packed/unpacked 结构体与数组类型，复用 TypeHelper 的 bitstream 分析，生成稳定长度与符号信息；只要是固定 bitstream 宽度的信号即可进入 memo。

## KR1 术语统一
- `SignalMemoEntry`、Memo driver 与相关接口统一改名为 net/reg，公开 API `peekNetMemo` 取代 `peekWireMemo`。
- 文档（`okr.md`、阶段5报告）同步调整描述，明确阶段目标是「net/reg」解析，消除历史术语混用。

## KR2 TypeHelper 增强
- `SignalMemoEntry` 新增 `fields`，每个字段记录 `path/msb/lsb/isSigned`，源于 `TypeHelper::analyze` 的 flatten 结果。
- `collectSignalMemos` 在注册候选时改为要求固定 bitstream 宽度，并通过 TypeHelper 一次性写入宽度、符号位与字段展开，支持 packed/unpacked struct 与多维数组。

## KR3 诊断优化
- 去掉阶段5遗留的 “Module body elaboration incomplete” TODO 诊断；net/reg 分析只关注本 Graph 内的信号，不再产生无意义噪声。

## 测试与样例
- `tests/data/elaborate/signal_memo.sv` 增加 struct、packed/unpacked array 信号：组合信号（net）通过 `assign`/`always_comb` 驱动，时序信号（reg）在 `always_ff` 中采样，覆盖 signed/unsigned mixed 结构。
- `tests/elaborate/test_elaborate_signal_memo.cpp`：
  - 使用 `peekNetMemo/peekRegMemo` 检查基础信号与新增复杂类型；
  - 验证 `SignalMemoEntry::fields` 记录的路径（如 `net_struct_bus.parts_hi[3]`、`net_unpacked_bus[1][0]`）及数量，确保 flatten 行为正确；
  - 仍然检查冲突信号的 NYI 诊断（消息改为 “net/reg”）。

## TypeHelper 与 SignalMemoEntry 细节
- TypeHelper 结构：
  - `TypeHelper::Info` 保存 `width`（`int64_t`）、`isSigned` 以及 `std::vector<Field>`。
  - `Field` 包含 `path/msb/lsb/isSigned`，对 packed/unpacked struct、数组进行递归 flatten，路径格式与 SystemVerilog 访问语义一致，例如 `bus.field[1][0]`。
  - 对非固定尺寸或非 bitstream 类型，会发出诊断并跳过 memo 记录，避免 GRH 中出现不可建模的值。
- SignalMemoEntry 结构：
  - `symbol` 和 `type` 指回 slang AST，便于后续 pass 读取源类型信息。
  - `width`/`isSigned` 直接来自 `TypeHelper::Info`，保证端口和值建模的一致性。
  - `fields` 镜像 `TypeHelper` 的展开结果，可用于定位具体 bit slice，配合 GRH `kSlice*`/`kConcat` 操作。
- 案例（来自 `memo_child`）：
  - `net_struct_bus`（类型 `memo_struct_t`）会在 memo 中生成 6 个字段：`parts_hi[3:0]` 与 `parts_lo[1:0]`，分别映射到总宽度 6 的 Value。
  - `net_unpacked_bus`（`logic signed [2:0] net_unpacked_bus [0:1]`）flatten 为连续 6 bit，字段路径形如 `net_unpacked_bus[1][0]`，可用来创建 `kSliceStatic` 以驱动单个数组元素。
  - `reg_packed_matrix`（`logic [1:0][3:0]`）保持 8 bit 宽度，字段 `reg_packed_matrix[0][0]`…`[1][3]` 体现 packed array 的字位顺序，测试中依此断言 flatten 行为。
