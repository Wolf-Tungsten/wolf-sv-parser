# Wolvrix App (Tcl/REPL) 规划草案

## 背景与目标
- **背景**：现有流程偏批处理风格；新需求希望像主流 EDA 工具一样支持 Tcl 脚本与交互式操作。
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
> 不提供兼容模式，命令体系重新设计。

## 核心架构草案
- **命令引擎**：统一的命令注册/执行层（支持 Tcl/REPL/CLI 三入口）。
- **会话状态**：
  - `Session` 持有唯一 GRH（单一 design）、诊断信息、默认路径、运行选项。
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
- 新 App 与既有批处理流程 **不兼容**，不提供自动转译。
- 通过文档与示例脚本指导用户迁移到新命令体系。

## 命令体系与命名规范（草案）
- 命令采用 `snake_case`，动词优先（`read_*`, `write_*`, `grh_*`, `transform`）。
- 选项统一为 `-key value`，布尔型用 `-flag`（无值）。
- 文件参数统一为显式命名（如 `-o`），避免位置参数歧义。
- 尽量避免与 Tcl 内建命令重名（如 `source`, `set` 仍保留 Tcl 语义）。
- 错误通过 `TCL_ERROR` 返回，结果通过命令返回值或 `last_error` 查询。
- 会话中 **有且仅有一个 design**；必须先 `read_*` 加载后才能运行变换/导出命令。
- 已加载 design 时 **禁止再次 `read_*`**；必须先 `close_design` 关闭后才能重新加载。
- `read_sv` 参数与 slang driver 标准参数对齐（复用 addStandardArgs），支持透传常用解析选项。

## GRH 工作流（单 design）
1) `read_sv` 或 `read_json` 加载 design。
2) `grh_*` 命令对 GRH 进行查询或修改。
3) `transform <passname> <passargs>` 在 design 上变换。
4) `write_sv` 或 `write_json` 输出结果。
> 未加载 design 时调用变换/导出命令，返回错误并提示先 `read_*`。
> 已加载 design 时再次 `read_*` 返回错误并提示先 `close_design`。

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
- `close_design`：关闭当前 GRH（释放 design），允许重新 `read_*`。

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
- 新增 `wolvrix` 可执行程序，移除旧批处理入口（无兼容层）。
- 嵌入 Tcl 解释器（vendored `external/tcl`）与 linenoise-ng（vendored `external/linenoise-ng`）。
- 支持 `-f <script.tcl>`/`-c <cmd>`/REPL 三入口；脚本执行会回显 Tcl 命令（跳过注释）。
- 统一 Session：单一 design；已加载时禁止再次 `read_*`，必须 `close_design`。
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
  - 顶层 `Makefile` 运行 C910 测试时固定执行 JSON round trip

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

## 分阶段路线与里程碑（延续版）
### Phase 1 — MVP 收口（交互可用 + 文档可用）
- **命令行为**：
  - 完成 `grh_select_graph` / `grh_delete_graph` / `grh_show_stats` 的行为补齐与帮助文本。
  - 统一 `write_json` 与 `write_sv` 的日志与输出统计（耗时、大小、路径）。
  - `read_sv` 增加最小“噪声过滤”策略（仅降级/静默已知低价值诊断）。
- **REPL 体验**：
  - 命令补全：命令名、选项名、文件路径。
  - 历史文件持久化（建议 `~/.wolvrix/history`），上限与去重策略可配置。
  - 统一提示符格式（显示 design 状态、当前目录）。
- **文档**：
  - `docs/cli/wolvrix-app.md`：快速上手 + 交互示例 + 基本工作流。
  - `docs/cli/command-reference.md`：完整命令条目与例子。
- **测试**：
  - 新增 Tcl 脚本端到端用例（读入→变换→导出→检查输出存在）。
  - C910 Tcl flow 回归复核（保持日志/产物路径一致）。

### Phase 2 — 可用性强化（稳定性 + 可扩展）
- **命令体系**：
  - 引入 `show_design` / `show_stats` / `show_modules` 统一展示。
  - `transform` 增加 `-list` / `-dryrun` 等可选参数（依赖 PassDiagnostics）。
  - 允许 `read_sv` 附带 `-top` 与 `-D` 简化参数（仅轻量封装，不破坏透传）。
- **会话与配置**：
  - `set_option`/`get_option` 支持常用项：`log.level`、`echo_tcl`、`history.enable`、
    `history.max_lines`、`output.dir`、`diagnostic.quiet`.
  - 增加 `reset_session` 或 `close_design` 的强制模式（确认/无确认）。
- **REPL**：
  - 多行输入与括号匹配提示（语法错误时友好回显）。
  - Ctrl-C/EOF 处理统一：中断当前命令，不退出 session。
- **测试**：
  - 针对每个命令的参数/错误路径的断言测试。
  - JSON round-trip 流水线：read_sv → write_json → read_json → write_sv → diff。

### Phase 3 — 生态与脚本化完善（脚本可复用 + 组合流）
- **脚本生态**：
  - 示例脚本库 `docs/cli/examples/`（HDLBits、C910、子模块提取等）。
  - 脚本模板（read/transform/write 的最小样例）。
- **扩展机制**：
  - 预留 `namespace`（例如 `wolvrix::`）避免 Tcl 命名冲突。
  - 提供 `register_command` 的内部接口（便于后续拓展）。
- **稳定性**：
  - 增加 `wolvrix -f` 批处理模式下的“严格失败”（遇错即退出）。
  - 日志输出支持 `-quiet`/`-verbose` 等一致接口。

## 命令行为与错误约定（细化）
- **返回值**：
  - 成功返回 Tcl `OK`（空或结构化返回值）。
  - 失败返回 `TCL_ERROR`，并设置 `last_error`（结构化字典字符串）。
- **last_error 结构建议**：
  - `code`（简短错误码，如 `NO_DESIGN`, `ARG_ERROR`, `IO_ERROR`）
  - `message`（用户可读文本）
  - `detail`（可选：子错误或诊断摘要）
- **统一参数规则**：
  - 所有命令以 `-key value` 为主，布尔 `-flag`。
  - 发现未知参数即报错并给出 `help <cmd>` 建议。
- **design 状态约束**：
  - 未加载 design：`transform` / `write_*` / `grh_*` 一律报错。
  - 已加载 design：`read_*` 一律报错，提示 `close_design`。

## REPL 交互细节（草案）
- **提示符**：
  - 形如：`wolvrix[loaded]>` / `wolvrix[empty]>`，可选显示 `cwd`。
- **历史**：
  - 默认开启；`history -n` 输出最近 N 条；`history clear` 清空。
- **补全策略**：
  - 首 token：命令名补全。
  - `-` 开头：当前命令的选项补全。
  - 其他：文件路径补全（基于 `pwd`）。
- **回显**：
  - `-f` 批处理：默认回显可关闭（`set_option echo_tcl 0`）。
  - REPL：仅在错误时回显上下文（避免噪声）。

## Session 选项（建议键）
- `log.level`：`error|warn|info|debug`
- `echo_tcl`：`0|1`
- `history.enable`：`0|1`
- `history.max_lines`：整数
- `output.dir`：输出默认目录
- `diagnostic.quiet`：诊断过滤级别或类别列表

## 代码落点建议（便于实现）
- `app/wolvrix/main.cpp`：入口与模式选择（REPL/`-f`/`-c`），含交互逻辑。
- `lib/src/cli_session.cpp`：Session 与状态管理（单 design 规则）。
- `lib/src/tcl_bindings.cpp`：命令注册与参数解析。
- `lib/src/cli_commands/*.cpp`：按类别拆分命令实现（read/write/transform/grh）。

## 测试矩阵（建议新增）
- **命令级**：
  - `read_sv`：缺参/文件不存在/语法错误/成功。
  - `write_sv`：无设计/路径无权限/成功。
  - `transform`：未知 pass/无设计/成功。
- **脚本级**：
  - `flow_ok.tcl`：read → transform → write。
  - `flow_fail.tcl`：触发错误并断言 `last_error`。
- **回归级**：
  - C910 Tcl flow 与旧日志对比一致性。
  - JSON round-trip diff（结构或语义等价）。

## 文档交付物（建议目录）
- `docs/cli/wolvrix-app.md`
  - 快速上手、REPL/脚本模式、示例。
- `docs/cli/command-reference.md`
  - 每个命令：功能、参数、返回值、错误、示例。
- `docs/cli/migration.md`
  - 迁移的步骤与常见替代命令。

## 验收标准（示例）
- 交互：REPL 有补全与历史，`help` 输出清晰且稳定。
- 脚本：`-f` 执行 10+ 条命令脚本，遇错可被 `last_error` 捕获。
- 功能：read/transform/write 流程在 HDLBits/C910 上可回归通过。
- 文档：命令参考覆盖率 100%，示例可直接运行。

## 开放问题（待确认）
- `last_error` 采用 **Tcl dict** 结构化输出（code/message/detail），REPL 失败时可额外打印 message。
- `read_sv` 的透传参数是否需要 whitelist（安全/一致性）？
- **不允许多个 design 共存**（仍保持单一 Session / 单一 design 模型）。
- REPL 提示符 **不显示** 当前 top/module（保持简洁）。
