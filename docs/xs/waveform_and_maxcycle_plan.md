# run_xs_ref_test Waveform 和 MaxCycle 功能实施方案

## 1. 背景与目标

为 `run_xs_ref_test` 目标添加与 c910 类似的波形 (waveform) 开关和最大 cycle 限制功能，使 XS (XiangShan) 仿真更加灵活可控。

## 2. 现状分析

### 2.1 c910 的实现方式

| 特性 | 实现方式 | 关键文件 |
|------|---------|---------|
| 最大 Cycle 限制 | 环境变量 `C910_SIM_MAX_CYCLE` | `tb_c910.cpp` 通过 `std::getenv()` 读取 |
| 波形开关 | 环境变量 `C910_WAVEFORM` (0/1) | `tb_c910.cpp` 通过 `std::getenv()` 读取 |
| 波形路径 | 环境变量 `C910_WAVEFORM_PATH` | `tb_c910.cpp` 通过 `std::getenv()` 读取 |
| 编译时波形支持 | Verilator `--trace-fst` 选项 | `smart_run/Makefile` |

### 2.2 XS 的现状

| 特性 | 现状 | 说明 |
|------|------|------|
| 最大 Cycle 限制 | 已支持 | 命令行参数 `-C, --max-cycles=NUM` |
| 波形开关 | 部分支持 | 编译时通过 `EMU_TRACE=fst` 启用，运行时通过 `--dump-wave` 开启 |
| 波形路径 | 已支持 | 命令行参数 `--wave-path=FILE` |
| 日志范围 | 已支持 | 命令行参数 `-b, --log-begin` 和 `-e, --log-end` |

### 2.3 关键差异

```
c910: 环境变量控制 → 适用于 Makefile 封装
xs:   命令行参数控制 → 需要在 Makefile 中转换为命令行参数
```

## 3. 实施方案

### 3.1 设计原则

1. **与 c910 保持一致的 Makefile 接口**：使用相同风格的环境变量命名
2. **复用 XS 现有的命令行参数机制**：不修改 XS 源码，仅通过 Makefile 传递参数
3. **向后兼容**：不设置新变量时，保持原有默认行为

### 3.2 Makefile 变量设计

| 变量名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `XS_SIM_MAX_CYCLE` | 整数 | `0` (无限制) | 最大仿真 cycle 数，0 表示无限制 |
| `XS_WAVEFORM` | 布尔 | `0` | 是否启用波形记录 (0/1) |
| `XS_WAVEFORM_PATH` | 路径 | 自动生成 | 波形文件输出路径 (`.fst` 格式) |
| `XS_WAVEFORM_DIR` | 目录 | `$(XS_LOG_DIR)` | 波形文件输出目录 |
| `XS_LOG_DIR` | 目录 | `$(BUILD_DIR)/logs/xs` | 日志输出目录 |

### 3.3 实施步骤

#### Step 1: 更新根目录 Makefile

在 `Makefile` 中添加 XS 相关的配置变量：

```makefile
# XiangShan simulation control
XS_SIM_MAX_CYCLE ?= 0
XS_WAVEFORM ?= 0
XS_LOG_DIR := $(BUILD_DIR)/logs/xs
XS_WAVEFORM_DIR ?= $(XS_LOG_DIR)
XS_WAVEFORM_DIR_ABS = $(abspath $(XS_WAVEFORM_DIR))
XS_WAVEFORM_PATH_ABS = $(if $(XS_WAVEFORM_PATH),$(if $(filter /%,$(XS_WAVEFORM_PATH)),$(XS_WAVEFORM_PATH),$(abspath $(XS_WAVEFORM_PATH))),)
```

#### Step 2: 重构 run_xs_ref_test 目标

```makefile
run_xs_ref_test:
	@echo "[RUN] Building XiangShan emu with EMU_THREADS=16..."
	$(MAKE) -C $(XS_ROOT) emu \
		EMU_THREADS=16 \
		NUM_CORES=1 \
		$(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,)
	@echo "[RUN] Running XiangShan emu..."
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(shell date +%Y%m%d_%H%M%S))
	@$(eval LOG_FILE := $(XS_LOG_DIR_ABS)/xs_ref_$(RUN_ID).log)
	@$(eval WAVEFORM_FILE := $(if $(XS_WAVEFORM_PATH_ABS),$(XS_WAVEFORM_PATH_ABS),$(XS_WAVEFORM_DIR_ABS)/xs_ref_$(RUN_ID).fst))
	@echo "[RUN] XS_SIM_MAX_CYCLE=$(XS_SIM_MAX_CYCLE) XS_WAVEFORM=$(XS_WAVEFORM)"
	@echo "[LOG] Capturing output to: $(LOG_FILE)"
	$(if $(filter 1,$(XS_WAVEFORM)),@echo "[WAVEFORM] Will save FST to: $(WAVEFORM_FILE)",)
	cd $(XS_ROOT) && ./build/emu \
		-i ./ready-to-run/coremark-2-iteration.bin \
		--diff ./ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 -e 0 \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(if $(filter 1,$(XS_WAVEFORM)),--dump-wave,) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $(WAVEFORM_FILE),) \
		2>&1 | tee "$(LOG_FILE)"
```

#### Step 3: 添加相关目录到 .gitignore

确保波形和日志文件不会被意外提交：

```
build/logs/xs/
*.fst
```

### 3.4 使用方法

#### 基本使用

```bash
# 运行 XS 参考测试（默认无限制，无波形）
make run_xs_ref_test

# 限制最大 10000 cycles
make run_xs_ref_test XS_SIM_MAX_CYCLE=10000

# 启用波形记录
make run_xs_ref_test XS_WAVEFORM=1

# 同时启用并限制 cycles
make run_xs_ref_test XS_SIM_MAX_CYCLE=10000 XS_WAVEFORM=1
```

#### 指定波形路径

```bash
# 指定波形文件路径
make run_xs_ref_test XS_WAVEFORM=1 XS_WAVEFORM_PATH=/tmp/my_waveform.fst

# 指定波形输出目录（自动生成文件名）
make run_xs_ref_test XS_WAVEFORM=1 XS_WAVEFORM_DIR=/tmp/xs_waves
```

### 3.5 与 c910 的对比

| 功能 | c910 | xs (实施后) |
|------|------|------------|
| 最大 Cycle | `C910_SIM_MAX_CYCLE=10000` | `XS_SIM_MAX_CYCLE=10000` |
| 波形开关 | `C910_WAVEFORM=1` | `XS_WAVEFORM=1` |
| 波形路径 | `C910_WAVEFORM_PATH=/path/to.fst` | `XS_WAVEFORM_PATH=/path/to.fst` |
| 日志目录 | `C910_LOG_DIR` | `XS_LOG_DIR` |

## 4. 技术细节

### 4.1 XS 仿真器的波形机制

XS 使用 Verilator 的 `--trace-fst` 选项编译时启用波形支持：

```makefile
# 在 verilator.mk 中
ifneq (,$(filter $(EMU_TRACE),fst FST))
VERILATOR_FLAGS += --trace-fst
VERILATOR_CXXFLAGS += -DENABLE_FST
endif
```

运行时通过 `--dump-wave` 参数启用波形记录，并结合 `-b/-e` 参数控制记录范围。

### 4.2 最大 Cycle 机制

XS 在 `emu.cpp` 中检查 cycle 限制：

```cpp
// 在 tick() 函数中
for (int i = 0; i < NUM_CORES; i++) {
  auto trap = difftest[i]->get_trap_event();
  if (trap->cycleCnt >= args.max_cycles) {
    exceed_cycle_limit = true;
  }
}
```

当 `max_cycles` 为 -1 (默认值) 时，表示无限制。

### 4.3 实现注意事项

1. **编译时波形支持**：由于 XS 需要重新编译才能启用/禁用波形，第一次启用 `XS_WAVEFORM=1` 时需要重新编译 emu
2. **日志输出**：XS 的 emu 本身会输出大量日志，使用 `tee` 同时显示和保存
3. **文件路径**：使用 `abspath` 确保路径正确解析
4. **EMU_THREADS 传递**：虽然 GNU Make 会自动传递命令行变量，但为了更清晰，建议显式传递 `EMU_THREADS` 给子 make

## 5. 后续扩展建议

### 5.1 短期扩展

- [ ] 添加 `run_xs_wolf_test` 目标，支持 wolf-sv-parser 处理后的 RTL
- [ ] 添加 `run_xs_diff` 目标，对比 ref 和 wolf 的仿真结果
- [ ] 支持指定测试用例（类似 c910 的 `CASE=`）

### 5.2 中期扩展

- [ ] 添加波形 diff 功能（类似 c910 的 `run_c910_diff`）
- [ ] 支持多核仿真配置
- [ ] 添加覆盖率收集选项

### 5.3 长期扩展

- [ ] 与 CI/CD 集成，自动运行回归测试
- [ ] 添加性能指标收集和对比
- [ ] 支持分布式仿真

## 6. 参考文档

- [c910 调试方案](../c910/openc910调试方案.md)
- [XiangShan 官方文档](https://xiangshan-doc.readthedocs.io/)
- [Verilator 文档](https://veripool.org/guide/latest/)
- [DiffTest 文档](https://github.com/OpenXiangShan/difftest)
