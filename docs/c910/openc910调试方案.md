# OpenC910 调试方案（通用模板）

目标：在 `tests/data/openc910/bug_cases` 下建立可复现、可对照、可回归的最小用例，
同时覆盖 wolf-sv-parser 编译复现、原始 RTL Verilator 基准和 wolf_emit.sv Verilator 对照。

## 目录结构约定

```
tests/data/openc910/bug_cases/case_xxx/
  filelist.f
  <minimized>.v
  tb_case_xxx.cpp
  bug_report.md
  Makefile
  coverage_check.py
```

说明：
- `filelist.f`：最小化后的 Verilog 文件列表（含 stub，如需要）。
- `tb_case_xxx.cpp`：Verilator C++ TB（直接驱动 DUT）。
- `bug_report.md`：复现步骤、现象、期望、定位记录。
- `Makefile`：统一的复现入口与清理。
- `coverage_check.py`：覆盖率阈值检查脚本。

## Makefile 目标（与顶层对齐）

- `run`：wolf-sv-parser 生成 `wolf_emit.sv`（使用 `TOP=DUT_TOP`，直接测试问题模块）。
- `run_c910_bug_case_ref`：Verilator 直接仿真 RTL（使用 `DUT_TOP`）。
- `run_c910_bug_case`：Verilator 仿真 `wolf_emit.sv`（使用 `DUT_TOP`）。
- `run_c910_bug_case_ref` 默认需带覆盖率报告（line coverage >= 90%）；若未满足应视为用例不完整。
- `run_c910_bug_case` 同样需打印覆盖率并检查阈值（默认 line coverage >= 90%）。
- `clean`：清除 `build/c910_bug_case/case_xxx`。

产物目录统一落在：
- `build/c910_bug_case/case_xxx/rtl`（RTL 仿真临时文件与可执行）
- `build/c910_bug_case/case_xxx/wolf`（wolf_emit.sv 与仿真临时文件）

## 新用例创建步骤

1. 新建目录：`tests/data/openc910/bug_cases/case_xxx/`。
2. 最小化提取 RTL（优先复用现有文件）：
   - 先定位报错模块与依赖链，尽量直接复用仓库内已有 RTL 文件。
   - `filelist.f` 优先列 `tests/data/openc910/C910_RTL_FACTORY` 内的原始 RTL 路径。
   - 非必要不要把 RTL 复制到 `case_xxx` 目录；需要新增 `.v` 时必须说明理由。
   - 只有在依赖过深且无法裁剪时，才新增 `stub_modules.v`，并记录原因。
3. 编写 `filelist.f`：
   - 只列入最小 RTL、必要的 `stub_modules.v`（如有）。
4. 编写 `tb_case_xxx.cpp`：
   - 直接实例化 `DUT_TOP`，包含 `V<DUT_TOP>.h`。
   - 提供 clock tick、reset、基础激励与断言。
   - 至少包含一组明确的正确性检查（未满足则返回非零）。
   - 只需覆盖关键行为，但要求确定性。
5. 参考模板 Makefile：
   - 定义 `TOP`、`DUT_TOP`、`OUT_DIR`、`WOLF_DIR`、`RTL_DIR`。
   - `run_c910_bug_case_ref` 走原始 `filelist.f`。
   - `run_c910_bug_case` 走 `wolf_emit.sv`。
6. 编写 `bug_report.md`：
   - 复现命令、现象、期望、最小化过程与初步分析。

## 复现流程

在 repo 根目录执行：

1. `make -C tests/data/openc910/bug_cases/case_xxx run`
2. `make -C tests/data/openc910/bug_cases/case_xxx run_c910_bug_case_ref`
3. `make -C tests/data/openc910/bug_cases/case_xxx run_c910_bug_case`

期望：
- `run_c910_bug_case_ref` 与 `run_c910_bug_case` 输出一致；
- `run_c910_bug_case_ref` 与 `run_c910_bug_case` 均需输出 line coverage 报告且覆盖率 >= 90%；
- 若一致但 wolf-sv-parser 在 `run` 阶段报错，则定位在解析/生成链路；
- 若不一致，优先对比 `wolf_emit.sv` 与原始 RTL 语义差异。

## 顶层一键入口

在 repo 根目录使用顶层 Makefile 快捷执行并自动清理：

- 单个用例：`make run_c910_bug_case CASE=case_xxx`
- 单个参考用例：`make run_c910_bug_case_ref CASE=case_xxx`
- 全部用例：`make run_c910_all_bug_case`

说明：
- 以上目标会先构建 `wolf-sv-parser`，再依次执行 `clean/run/run_c910_bug_case_ref/run_c910_bug_case`。
- 若需要跳过构建，可加 `SKIP_WOLF_BUILD=1`。

## 最小化提取建议

- 从报错符号出发定位模块，再向下裁剪依赖。
- filelist 复用 `C910_RTL_FACTORY` 原始 RTL 为主，非必要不要创建或复制 `.v`。
- 保持参数定义与端口宽度一致；必要时在 TB 中显式设置参数。
- 优先保留时序结构（always 块/复位/时钟），删除无关逻辑。
- RTL 与 wolf_emit 双路径同时验证，避免“看似修复但行为不一致”。

## 产物与清理

所有临时产物都在 `build/c910_bug_case/case_xxx/`，不应提交。
覆盖率产物：
- RTL 路径：`build/c910_bug_case/case_xxx/rtl/coverage.dat`、`build/c910_bug_case/case_xxx/rtl/coverage.info`
- wolf 路径：`build/c910_bug_case/case_xxx/wolf/coverage.dat`、`build/c910_bug_case/case_xxx/wolf/coverage.info`
使用 `make -C tests/data/openc910/bug_cases/case_xxx clean` 清理。
