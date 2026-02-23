# Wolvrix App (Tcl/REPL) 升级替换 wolvrix-cli 规划草案

## 背景与目标
- **背景**：现有 `wolvrix-cli` 偏批处理风格；新需求希望像主流 EDA 工具一样支持 Tcl 脚本与交互式操作。
- **目标**：
  - 支持 `source` 执行 Tcl 脚本、变量/流程控制、以及工具命令。
  - 支持 REPL 交互（含命令历史、自动补全、上下文状态）。
  - 复用当前核心库（`wolvrix-lib`）能力，尽量减少重复实现。
  - 提供新命令体系的脚本示例与迁移指引。

## 范围与非目标
- **范围**：
  - 新 CLI/REPL 入口与命令系统。
  - Tcl 命令绑定层与核心库桥接。
  - 基础交互体验（历史、help、错误提示）。
- **非目标（阶段 1）**：
  - GUI/可视化；分布式调度；完整 EDA flow 管理（后续再评估）。

## 体验与用法草案
- 批处理脚本：
  - `wolvrix -f flow.tcl`
  - `wolvrix -c "read_verilog a.sv; emit_sv -o out.sv"`
- 交互 REPL：
  - `wolvrix` 进入交互
  - `help` / `help read_verilog`
  - `history`, `quit/exit`
> 不提供旧 CLI 兼容模式，命令体系重新设计。

## 核心架构草案
- **命令引擎**：统一的命令注册/执行层（支持 Tcl/REPL/CLI 三入口）。
- **会话状态**：
  - `Session` 持有唯一 GRH（单一 netlist）、诊断信息、默认路径、运行选项。
  - 命令执行以 `Session` 为上下文，便于 REPL 持久化。
- **诊断与日志**：
  - 保持现有 `ConvertDiagnostics`/`EmitDiagnostics` 风格。
  - Tcl/REPL 层错误统一格式化输出。

## Tcl 集成方案
- **嵌入 Tcl 解释器（选定）**
  - 优点：生态成熟、语法完整、与 EDA 传统对齐。
  - 风险：引入额外依赖与构建复杂度。

> 已确认：Linux only，采用 vendored Tcl（放入 `external/tcl`），以 git submodule 固定到 9.0.3，由 CMake 统一编译与链接。
> 版本策略：锁定 9.0.3；9.1 预览版本不作为生产依赖。

## 迁移策略
- 新 App 与旧 CLI **不兼容**，不提供自动转译。
- 通过文档与示例脚本指导用户迁移到新命令体系。

## 命令体系与命名规范（草案）
- 命令采用 `snake_case`，动词优先（`read_*`, `write_*`, `grh_*`, `transform`）。
- 选项统一为 `-key value`，布尔型用 `-flag`（无值）。
- 文件参数统一为显式命名（如 `-o`），避免位置参数歧义。
- 尽量避免与 Tcl 内建命令重名（如 `source`, `set` 仍保留 Tcl 语义）。
- 错误通过 `TCL_ERROR` 返回，结果通过命令返回值或 `last_error` 查询。
- 会话中 **有且仅有一个 netlist**；必须先 `read_*` 加载后才能运行变换/导出命令。
- 已加载 netlist 时 **禁止再次 `read_*`**；必须先 `close_design` 关闭后才能重新加载。
- `read_sv` 参数与 slang driver 标准参数对齐（复用 addStandardArgs），支持透传常用解析选项。

## GRH 工作流（单 netlist）
1) `read_sv` 或 `read_json` 加载 netlist。
2) `grh_*` 命令对 GRH 进行查询或修改。
3) `transform <passname> <passargs>` 在 netlist 上变换。
4) `write_sv` 或 `write_json` 输出结果。
> 未加载 netlist 时调用变换/导出命令，返回错误并提示先 `read_*`。
> 已加载 netlist 时再次 `read_*` 返回错误并提示先 `close_design`。

## 命令参考草案（初版）
### 会话/交互
- `help ?cmd?`：显示命令帮助。
- `version`：打印版本与构建信息。
- `exit` / `quit`：退出 REPL。
- `history ?-n <count>?`：显示历史记录。
- `source <file.tcl>`：执行脚本（Tcl 内建）。

### 项目/路径
- `pwd`：显示当前工作目录。
- `cd <dir>`：切换工作目录。
- `set_option <key> <value>`：设置会话选项（可映射到 Session）。
- `get_option <key>`：读取会话选项。

### 输入/加载
- `read_sv <file> ?<file>...? ?<slang-opts>...?`
- `read_sv` 支持 slang driver 的标准参数（示例：`--std`, `-I/+incdir`, `--isystem`,
  `-D/-U`, `--top`, `-G`, `-y/-Y`, `-f/-F`, `--single-unit`, `--timescale` 等）。
- `read_json <file>`
- `close_design`：关闭当前 GRH（释放 netlist），允许重新 `read_*`。

### GRH 操作
- `grh_list_graph`
- `grh_create_graph <name>`
- `grh_select_graph <name>`
- `grh_delete_graph <name>`
- `grh_show_stats`

### 变换/优化
- `transform <passname> <passargs>`
- `transform_list`

### 输出/导出
- `write_sv -o <file>`
- `write_json -o <file>`

### 诊断/状态
- `show_modules`
- `show_stats`
- `last_error`

## 文档与测试计划
- 文档：
  - `docs/cli/wolvrix-app.md`（用户指南）
  - `docs/cli/command-reference.md`（命令参考）
- 测试：
  - 新增 `tests/cli` 或 `tests/ingest` 下的 Tcl/REPL 用例。
  - 关键命令端到端回归测试（脚本驱动）。

## 风险与注意事项
- REPL 基于 linenoise-ng v1.0.1（功能增强，可 vendored）；高级体验仍需在此基础上补齐功能。
- Tcl 9.0 引入不兼容点，需要对脚本/命令进行适配评估。

## 当前实现（已完成）
- 新增 `wolvrix` 可执行程序，保留 `wolvrix-cli` 仅作存量使用（无兼容层）。
- 嵌入 Tcl 解释器（vendored `external/tcl`）与 linenoise-ng（vendored `external/linenoise-ng`）。
- 支持 `-f <script.tcl>`/`-c <cmd>`/REPL 三入口；脚本执行会回显 Tcl 命令（跳过注释）。
- 统一 Session：单一 netlist；已加载时禁止再次 `read_*`，必须 `close_design`。
- 实装命令：
  - `help`
  - `read_sv` / `read_json` / `close_design`
  - `transform` / `transform_list`
  - `write_sv` / `write_json`
  - `grh_list_graph` / `grh_create_graph` / `grh_show_stats`
- `read_sv` 透传 slang driver 参数；发生语法/解析错误时输出 slang 诊断。
- 日志输出改为“实体 + 时间 + 级别 + 内容”格式；transform 按 pass 输出；`write_sv` 输出耗时、路径、大小。
- Welcome/Goodbye 使用框线展示：欢迎信息含版本/commit；退出时输出总耗时与最大内存占用。
- C910 flow 已迁移到 Tcl：
  - 新增 `tests/data/openc910/smart_run/wolvrix.tcl`
  - `tests/data/openc910/smart_run/Makefile` 使用 `wolvrix` 产物（输出统一到 `build/`）
  - 顶层 `Makefile` 合并 `run_c910_json_test` 为 `JSON_ROUND_TRIP` 开关

## 下一步待完善
- **文档**：
  - 新增/完善 `docs/cli/wolvrix-app.md` 与命令参考。
  - C910/Tcl flow 的使用说明与参数说明。
- **交互体验**：
  - REPL 自动补全、历史管理细化、`help` 输出优化。
  - 日志与回显的最后细节（格式/静默规则）统一对齐。
- **功能补齐**：
  - `write_json` 输出日志与大小统计。
  - `read_sv` 诊断过滤/分级策略完善（仅静默特定噪声）。
  - 增加更多 GRH 操作命令（如删除/选择 graph）。
- **测试**：
  - Tcl 脚本端到端测试（read/transform/write）。
  - C910 flow 回归用例与 JSON round-trip 覆盖。
