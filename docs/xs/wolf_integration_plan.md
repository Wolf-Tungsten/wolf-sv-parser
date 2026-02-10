# XiangShan wolf-sv-parser 集成路线（run_xs_test / run_xs_diff）

## 目标

1. 复用 `run_xs_ref_test` 的编译/运行体系，新增 **wolf 版本**：`run_xs_test`。
2. 引入 **可并行比对** 的 `run_xs_diff`，同时跑 ref 与 wolf，输出独立日志与波形。
3. 让 wolf-sv-parser **处理 XiangShan + difftest 生成的 SystemVerilog**，输出 `wolf_emit.sv`，再与 C++ TB（difftest/emu）链接。

---

## 现有 `run_xs_ref_test` 编译结构（现状梳理）

顶层入口：`Makefile: run_xs_ref_test`

流程概要：

```
make run_xs_ref_test
  └─ make -C tests/data/xiangshan emu
      ├─ sim-verilog
      │   └─ mill 生成 SystemVerilog 到 tests/data/xiangshan/build/rtl/
      │       └─ 关键输出：SimTop.sv + 其他 *.sv
      └─ make -C tests/data/xiangshan/difftest emu
          ├─ verilator.mk
          │   ├─ SIM_TOP_V = $(RTL_DIR)/SimTop.sv
          │   ├─ RTL_DIR  = $(BUILD_DIR)/rtl
          │   └─ 组合 vsrc/csrc + 生成的 RTL 编译 C++ 仿真器
          └─ 输出：tests/data/xiangshan/build/emu
  └─ ./build/emu -i coremark ... --diff nemu.so
```

关键文件：
- `Makefile`（顶层）：`run_xs_ref_test`
- `tests/data/xiangshan/Makefile`：`sim-verilog` / `emu`
- `tests/data/xiangshan/difftest/Makefile` + `emu.mk` + `verilator.mk`

环境注意：XiangShan 侧要求 `NOOP_HOME` 指向 XiangShan 根目录。

---

## 目标态总体思路

新增 **wolf 流水**，在 ref 的 RTL 生成之后插入 `wolf_emit`：

```
(sim-verilog 生成 RTL) + (difftest 生成/提供 SV 资源)
          │
          └─ wolf-sv-parser  -> wolf_emit.sv
                            └─ difftest/emu 用 wolf_emit.sv 构建仿真器
```

并行 diff：`run_xs_diff` 同时跑 ref / wolf，日志、波形与构建目录彼此隔离。

---

## 规划路线（分阶段实施）

### Phase 1: 目录与变量规范化（确保并行安全）

新增约定目录（避免 ref/wolf 互相覆盖）：

- `XS_WORK_BASE = build/xs`
- `XS_REF_BUILD = $(XS_WORK_BASE)/ref`
- `XS_WOLF_BUILD = $(XS_WORK_BASE)/wolf`
- `XS_WOLF_EMIT_DIR = $(XS_WOLF_BUILD)/wolf_emit`
- `XS_WOLF_EMIT = $(XS_WOLF_EMIT_DIR)/wolf_emit.sv`
- `XS_WOLF_FILELIST = $(XS_WOLF_EMIT_DIR)/xs_wolf.f`

说明：
- XiangShan 本体 `sim-verilog` 与 difftest/emu 编译应统一指向 **同一 build 根**。
- ref/wolf 需使用不同 `BUILD_DIR`/`RTL_DIR`/`VERILATOR` 输出路径。

### Phase 2: RTL 收集与 filelist 生成

目标：收集 **XiangShan + difftest 的 SystemVerilog** 并生成 filelist。

建议流程：
1. `make -C $(XS_ROOT) sim-verilog BUILD_DIR=$(XS_REF_BUILD)`
2. （如需要）`make -C $(XS_ROOT)/difftest difftest_verilog DESIGN_DIR=$(XS_REF_BUILD)`
3. 生成 filelist：
   - 主体：`$(XS_REF_BUILD)/rtl/*.sv`
   - difftest 侧若有额外生成 SV（`difftest_verilog` 输出）也纳入
   - 维持稳定顺序（例如按字典序 + `SimTop.sv` 最后）

wolf-sv-parser 还需 include/define：
- `-I $(XS_REF_BUILD)/rtl`
- `-I $(XS_ROOT)/difftest/src/test/vsrc/common`
- 需要同步 `SIM_VFLAGS` 中的 `+define+...`（如 `DIFFTEST`）

建议在顶层 Makefile 抽象：
- `XS_SIM_DEFINES`（共享给 wolf-sv-parser 与 Verilator）
- `XS_SIM_INCLUDES`（共享 include 路径）

### Phase 3: wolf_emit 生成

使用 wolf-sv-parser 产出单文件：

```
$(WOLF_PARSER) \
  --emit-sv --top SimTop \
  -o $(XS_WOLF_EMIT) \
  -f $(XS_WOLF_FILELIST) \
  $(XS_WOLF_EMIT_FLAGS) \
  $(XS_SIM_INCLUDES) $(XS_SIM_DEFINES)
```

推荐默认输出：
- `wolf_emit.sv`
- （可选）`grh.json`

### Phase 4: wolf 版本仿真器构建

复用 difftest/emu 体系，但 **指定 wolf_emit.sv 为顶层输入**：

建议参数覆盖：
- `SIM_TOP_V=$(XS_WOLF_EMIT)`
- `RTL_DIR=$(XS_WOLF_EMIT_DIR)`
- `BUILD_DIR=$(XS_WOLF_BUILD)`
- `NUM_CORES=1` / `EMU_THREADS` / `EMU_TRACE`（与 ref 同步）

调用方式示例：

```
make -C $(XS_ROOT)/difftest emu \
  SIM_TOP_V=$(XS_WOLF_EMIT) RTL_DIR=$(XS_WOLF_EMIT_DIR) \
  BUILD_DIR=$(XS_WOLF_BUILD) NUM_CORES=1 RTL_SUFFIX=sv
```

输出：`$(XS_WOLF_BUILD)/emu`

### Phase 5: 新增 run_xs_test / run_xs_diff

#### run_xs_test（wolf 版本）
建议步骤：
1. 确保 wolf-sv-parser 已构建（支持 `SKIP_WOLF_BUILD=1`）
2. 生成 ref RTL（sim-verilog）
3. 生成 wolf_emit.sv
4. 构建 wolf emu
5. 运行 `emu`（参数对齐 ref：`-i` / `--diff` / `-C` / `--dump-wave`）

#### run_xs_diff（并行比对）
类似 `run_c910_diff`：
- 生成独立 log / fst
- ref/wolf 使用 **不同 build 目录**
- `&` 并行启动，`wait` 汇总状态

---

## 关键一致性要求（务必对齐）

1. **宏定义一致**：wolf-sv-parser 与 Verilator 解析同一套 `+define+`。
2. **include 一致**：解析与仿真使用相同 `-I` 搜索路径。
3. **SimTop 一致**：wolf_emit 必须以 `SimTop` 为顶层。
4. **build 目录隔离**：并行 diff 时 ref/wolf 不得共用 `build/`。

---

## 建议的 Makefile 变量清单（草案）

- `XS_WORK_BASE` / `XS_REF_BUILD` / `XS_WOLF_BUILD`
- `XS_WOLF_EMIT_DIR` / `XS_WOLF_EMIT` / `XS_WOLF_FILELIST`
- `XS_SIM_DEFINES` / `XS_SIM_INCLUDES`
- `XS_WOLF_EMIT_FLAGS`
- `XS_SIM_MAX_CYCLE` / `XS_WAVEFORM` / `XS_WAVEFORM_PATH`

---

## 里程碑与验证点

1. **wolf_emit 能生成**（无报错）：`wolf_emit.sv` 产出成功。
2. **wolf emu 可编译**：Verilator 编译通过（无致命 error）。
3. **wolf emu 可运行**：能跑 coremark 启动流程。
4. **run_xs_diff 并行完成**：日志与波形路径正确，无资源冲突。

---

## 示例用法（目标形态）

```
# 单独跑 wolf 版本
make run_xs_test XS_SIM_MAX_CYCLE=5000 XS_WAVEFORM=1

# 并行比对
make run_xs_diff XS_SIM_MAX_CYCLE=5000 XS_WAVEFORM=1
```

---

## 后续可选增强

- 增加 `XS_CASE` / workload 选择接口
- 对齐 c910 的 `WORK_DIR` 机制与日志格式
- 加入 waveform diff / 自动信号采样

