SHELL := /bin/bash

# Check env.sh must exist and has been sourced
ENV_FILE := $(if $(wildcard $(CURDIR)/../env.sh),$(CURDIR)/../env.sh,$(CURDIR)/env.sh)
ifeq (,$(wildcard $(ENV_FILE)))
    $(error env.sh not found. Please run: cp ../env.sh.template ../env.sh && source ../env.sh)
endif

ifeq ($(WOLF_ENV_SOURCED),)
    $(error env.sh exists but not sourced. Please run: source $(ENV_FILE))
endif

# Auto-load environment from env.sh
export TOOL_EXTENSION := $(or $(TOOL_EXTENSION),$(shell grep '^export TOOL_EXTENSION=' $(ENV_FILE) 2>/dev/null | cut -d'"' -f2))
export VERILATOR := $(or $(VERILATOR),$(shell grep '^export VERILATOR=' $(ENV_FILE) 2>/dev/null | cut -d'"' -f2))
BUILD_DIR ?= build
CMAKE ?= cmake
WOLVRIX_APP := $(BUILD_DIR)/bin/wolvrix
RUN_ID ?= $(shell date +%Y%m%d_%H%M%S)

# Verilator path (can be overridden via environment or env.sh)
VERILATOR ?= $(or $(shell echo $$VERILATOR),verilator)
VERILATOR_FLAGS ?= -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNDRIVEN \
	-Wno-SYNCASYNCNET

SINGLE_THREAD ?= 0
ifeq ($(origin WOLF_LOG), undefined)
WOLF_LOG := info
WOLF_LOG_DEFAULT := 1
else
WOLF_LOG_DEFAULT := 0
endif
WOLF_TIMER ?= 0
WOLF_TIMEOUT ?= 600
WOLF_EMIT_FLAGS ?=

ifneq ($(strip $(WOLF_TIMER)),0)
WOLF_LOG := debug
endif
ifneq ($(strip $(SINGLE_THREAD)),0)
WOLF_EMIT_FLAGS += --single-thread
endif
ifneq ($(strip $(WOLF_LOG)),)
WOLF_EMIT_FLAGS += --log $(WOLF_LOG)
endif
ifneq ($(strip $(WOLF_TIMER)),0)
WOLF_EMIT_FLAGS += --profile-timer
endif
ifneq ($(strip $(WOLF_TIMEOUT)),)
WOLF_EMIT_FLAGS += --timeout $(WOLF_TIMEOUT)
endif

XS_ROOT := tests/data/xiangshan
XS_WOLVRIX_SCRIPT := $(abspath $(CURDIR)/../scripts/wolvrix_emit.tcl)

# XiangShan paths / options
XS_SIM_MAX_CYCLE ?= 0
XS_WAVEFORM ?= 0
XS_WAVEFORM_FULL ?= 0
XS_RAM_TRACE ?= 0
XS_NUM_CORES ?= 1
XS_EMU_THREADS ?= 4
XS_SIM_TOP ?= SimTop
XS_RTL_SUFFIX ?= sv
XS_WITH_CHISELDB ?= 0
XS_WITH_CONSTANTIN ?= 0
XS_ZERO_INIT ?= 0
ifeq ($(XS_ZERO_INIT),1)
XS_ZERO_INIT_DEFINES := RANDOMIZE_REG_INIT RANDOMIZE_MEM_INIT RANDOMIZE_DELAY=0 RANDOM=32'h0
else
XS_ZERO_INIT_DEFINES :=
endif
XS_ZERO_INIT_SIM_DEFINES := $(subst 32'h0,32\'h0,$(XS_ZERO_INIT_DEFINES))
XS_SIM_VFLAGS ?= +define+DIFFTEST $(foreach d,$(XS_ZERO_INIT_SIM_DEFINES),+define+$(d))
XS_EMU_PREFIX ?= $(shell if command -v stdbuf >/dev/null 2>&1; then echo "stdbuf -oL -eL"; fi)
XS_RAM_TRACE_ARGS := $(if $(filter 1,$(XS_RAM_TRACE)),+trace_difftest_ram,)
XS_LOG_DIR := $(BUILD_DIR)/logs/xs
XS_WAVEFORM_DIR ?= $(XS_LOG_DIR)
XS_LOG_DIR_ABS = $(abspath $(XS_LOG_DIR))
XS_WAVEFORM_DIR_ABS = $(abspath $(XS_WAVEFORM_DIR))
XS_WAVEFORM_PATH_ABS = $(if $(XS_WAVEFORM_PATH),$(if $(filter /%,$(XS_WAVEFORM_PATH)),$(XS_WAVEFORM_PATH),$(abspath $(XS_WAVEFORM_PATH))),)

XS_WORK_BASE ?= $(BUILD_DIR)/xs
XS_RTL_BUILD ?= $(XS_WORK_BASE)/rtl
XS_REF_BUILD ?= $(XS_WORK_BASE)/ref
XS_WOLF_BUILD ?= $(XS_WORK_BASE)/wolf
XS_RTL_DIR := $(XS_RTL_BUILD)/rtl
XS_VSRC_DIR ?= $(XS_ROOT)/difftest/src/test/vsrc/common
XS_WOLF_EMIT_DIR ?= $(XS_WOLF_BUILD)/wolf_emit
XS_WOLF_EMIT ?= $(XS_WOLF_EMIT_DIR)/wolf_emit.sv
XS_WOLF_FILELIST ?= $(XS_WOLF_EMIT_DIR)/xs_wolf.f
XS_SIM_DEFINES ?= DIFFTEST
XS_SIM_DEFINES += $(XS_ZERO_INIT_DEFINES)
XS_WOLF_EMIT_FLAGS ?=

XS_WORK_BASE_ABS := $(abspath $(XS_WORK_BASE))
XS_ROOT_ABS := $(abspath $(XS_ROOT))
XS_RTL_BUILD_ABS := $(abspath $(XS_RTL_BUILD))
XS_REF_BUILD_ABS := $(abspath $(XS_REF_BUILD))
XS_WOLF_BUILD_ABS := $(abspath $(XS_WOLF_BUILD))
XS_RTL_DIR_ABS := $(abspath $(XS_RTL_DIR))
XS_VSRC_DIR_ABS := $(abspath $(XS_VSRC_DIR))
XS_WOLF_EMIT_DIR_ABS := $(abspath $(XS_WOLF_EMIT_DIR))
XS_WOLF_EMIT_ABS := $(abspath $(XS_WOLF_EMIT))
XS_WOLF_FILELIST_ABS := $(abspath $(XS_WOLF_FILELIST))
XS_SIM_TOP_V := $(XS_RTL_DIR_ABS)/$(XS_SIM_TOP).$(XS_RTL_SUFFIX)
XS_WOLF_JSON ?= $(XS_WOLF_EMIT_DIR_ABS)/xs_wolf.json
XS_JSON_ROUNDTRIP ?= 0
XS_WOLF_JSON_EMIT_FLAGS ?= --store-json --top $(XS_SIM_TOP)
XS_WOLF_JSON_LOAD_FLAGS ?= --emit-sv --top $(XS_SIM_TOP)

XS_DIFFTEST_GEN_DIR ?= $(XS_ROOT)/build/generated-src
XS_DIFFTEST_GEN_DIR_ABS := $(abspath $(XS_DIFFTEST_GEN_DIR))
XS_WOLF_INCLUDE_DIRS ?= $(XS_RTL_DIR_ABS) $(XS_VSRC_DIR_ABS) $(XS_DIFFTEST_GEN_DIR_ABS)
XS_WOLF_INCLUDE_FLAGS := $(foreach d,$(XS_WOLF_INCLUDE_DIRS),-I $(d))
XS_WOLF_DEFINE_FLAGS := $(foreach d,$(XS_SIM_DEFINES),-D "$(d)")
XS_WOLF_DEFINE_FLAGS_LOG := $(foreach d,$(XS_SIM_DEFINES),-D $(d))

.PHONY: all build \
	xs_rtl xs_wolf_filelist xs_wolf_emit xs_ref_emu xs_wolf_emu run_xs_json_test \
	xs_diff_clean run_xs_ref_emu run_xs_wolf_emu run_xs_diff clean

all: build

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) 

$(WOLVRIX_APP): build

# XiangShan: generate sim-verilog
$(XS_SIM_TOP_V):
	@echo "[RUN] Generating XiangShan sim-verilog into $(XS_RTL_BUILD_ABS)..."
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval LOG_FILE := $(XS_LOG_DIR_ABS)/xs_simverilog_$(RUN_ID).log)
	@echo "[LOG] Recording sim-verilog command to: $(LOG_FILE)"
	@printf '' > "$(LOG_FILE)"
	@echo "[CMD] $(MAKE) -C $(XS_ROOT) sim-verilog BUILD_DIR=$(XS_RTL_BUILD_ABS) NUM_CORES=$(XS_NUM_CORES) RTL_SUFFIX=$(XS_RTL_SUFFIX)" | tee -a "$(LOG_FILE)"
	$(MAKE) -C $(XS_ROOT) sim-verilog \
		BUILD_DIR=$(XS_RTL_BUILD_ABS) \
		NUM_CORES=$(XS_NUM_CORES) \
		RTL_SUFFIX=$(XS_RTL_SUFFIX)

xs_rtl: $(XS_SIM_TOP_V)

$(XS_WOLF_FILELIST_ABS): $(XS_SIM_TOP_V)
	@mkdir -p "$(dir $@)"
	@{ \
		find "$(XS_RTL_DIR_ABS)" -type f -name "*.sv" -o -type f -name "*.v"; \
		find "$(XS_VSRC_DIR_ABS)" -type f -name "*.sv" -o -type f -name "*.v"; \
	} | LC_ALL=C sort > "$@"

xs_wolf_filelist: $(XS_WOLF_FILELIST_ABS)

ifneq ($(strip $(SKIP_WOLF_BUILD)),1)
XS_WOLF_DEPS := $(WOLVRIX_APP)
endif

xs_wolf_emit: $(XS_WOLF_FILELIST_ABS) $(XS_WOLF_DEPS)
	@mkdir -p "$(XS_WOLF_EMIT_DIR_ABS)"
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(RUN_ID))
	@$(eval BUILD_LOG_FILE := $(XS_LOG_DIR_ABS)/xs_wolf_build_$(RUN_ID).log)
	@$(eval READ_ARGS_FILE := $(XS_WOLF_EMIT_DIR_ABS)/wolvrix_read_args.txt)
	@echo "[LOG] Capturing wolf emit output to: $(BUILD_LOG_FILE)"
	@printf '' > "$(BUILD_LOG_FILE)"
	@printf '' > "$(READ_ARGS_FILE)"
	@printf "%s\n" $(XS_WOLF_INCLUDE_FLAGS) $(XS_WOLF_DEFINE_FLAGS) >> "$(READ_ARGS_FILE)"
	@{ \
		if [ "$(XS_JSON_ROUNDTRIP)" = "1" ]; then \
			echo "[CMD] WOLVRIX_FILELIST=$(XS_WOLF_FILELIST_ABS) WOLVRIX_TOP=$(XS_SIM_TOP) WOLVRIX_SV_OUT=$(XS_WOLF_EMIT_ABS) WOLVRIX_JSON_OUT=$(XS_WOLF_JSON) WOLVRIX_JSON_ROUNDTRIP=1 WOLVRIX_READ_ARGS_FILE=$(READ_ARGS_FILE) WOLVRIX_LOG_LEVEL=$(WOLF_LOG) $(WOLVRIX_APP) -f $(XS_WOLVRIX_SCRIPT)"; \
			WOLVRIX_FILELIST=$(XS_WOLF_FILELIST_ABS) \
			WOLVRIX_TOP=$(XS_SIM_TOP) \
			WOLVRIX_SV_OUT=$(XS_WOLF_EMIT_ABS) \
			WOLVRIX_JSON_OUT=$(XS_WOLF_JSON) \
			WOLVRIX_JSON_ROUNDTRIP=1 \
			WOLVRIX_READ_ARGS_FILE=$(READ_ARGS_FILE) \
			WOLVRIX_LOG_LEVEL=$(WOLF_LOG) \
			$(WOLVRIX_APP) -f $(XS_WOLVRIX_SCRIPT); \
		else \
			echo "[CMD] WOLVRIX_FILELIST=$(XS_WOLF_FILELIST_ABS) WOLVRIX_TOP=$(XS_SIM_TOP) WOLVRIX_SV_OUT=$(XS_WOLF_EMIT_ABS) WOLVRIX_READ_ARGS_FILE=$(READ_ARGS_FILE) WOLVRIX_LOG_LEVEL=$(WOLF_LOG) $(WOLVRIX_APP) -f $(XS_WOLVRIX_SCRIPT)"; \
			WOLVRIX_FILELIST=$(XS_WOLF_FILELIST_ABS) \
			WOLVRIX_TOP=$(XS_SIM_TOP) \
			WOLVRIX_SV_OUT=$(XS_WOLF_EMIT_ABS) \
			WOLVRIX_READ_ARGS_FILE=$(READ_ARGS_FILE) \
			WOLVRIX_LOG_LEVEL=$(WOLF_LOG) \
			$(WOLVRIX_APP) -f $(XS_WOLVRIX_SCRIPT); \
		fi; \
	} 2>&1 | tee -a "$(BUILD_LOG_FILE)"

xs_ref_emu: $(XS_SIM_TOP_V)
	@echo "[RUN] Building XiangShan ref emu..."
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(if $(RUN_ID),$(RUN_ID),$(shell date +%Y%m%d_%H%M%S)))
	@$(eval BUILD_LOG_FILE := $(XS_LOG_DIR_ABS)/xs_ref_build_$(RUN_ID).log)
	@echo "[LOG] Capturing build output to: $(BUILD_LOG_FILE)"
	@printf '' > "$(BUILD_LOG_FILE)"
	@echo "[CMD] $(MAKE) -C $(XS_ROOT)/difftest emu BUILD_DIR=$(XS_REF_BUILD_ABS) GEN_CSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) GEN_VSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) RTL_DIR=$(XS_RTL_DIR_ABS) SIM_TOP_V=$(XS_SIM_TOP_V) NUM_CORES=$(XS_NUM_CORES) RTL_SUFFIX=$(XS_RTL_SUFFIX) EMU_THREADS=$(XS_EMU_THREADS) EMU_RANDOMIZE=0 SIM_VFLAGS=\"$(XS_SIM_VFLAGS)\" WITH_CHISELDB=$(XS_WITH_CHISELDB) WITH_CONSTANTIN=$(XS_WITH_CONSTANTIN) $(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,)" | tee -a "$(BUILD_LOG_FILE)"
	$(MAKE) -C $(XS_ROOT)/difftest emu \
		BUILD_DIR=$(XS_REF_BUILD_ABS) \
		GEN_CSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) \
		GEN_VSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) \
		RTL_DIR=$(XS_RTL_DIR_ABS) \
		SIM_TOP_V=$(XS_SIM_TOP_V) \
		NUM_CORES=$(XS_NUM_CORES) \
		RTL_SUFFIX=$(XS_RTL_SUFFIX) \
		EMU_THREADS=$(XS_EMU_THREADS) \
		EMU_RANDOMIZE=0 \
		SIM_VFLAGS="$(XS_SIM_VFLAGS)" \
		WITH_CHISELDB=$(XS_WITH_CHISELDB) \
		WITH_CONSTANTIN=$(XS_WITH_CONSTANTIN) \
		$(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,) \
		2>&1 | tee "$(BUILD_LOG_FILE)"

xs_wolf_emu: xs_wolf_emit
	@echo "[RUN] Building XiangShan wolf emu..."
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(if $(RUN_ID),$(RUN_ID),$(shell date +%Y%m%d_%H%M%S)))
	@$(eval BUILD_LOG_FILE := $(XS_LOG_DIR_ABS)/xs_wolf_build_$(RUN_ID).log)
	@echo "[LOG] Capturing build output to: $(BUILD_LOG_FILE)"
	@printf '' >> "$(BUILD_LOG_FILE)"
	@echo "[CMD] $(MAKE) -C $(XS_ROOT)/difftest emu BUILD_DIR=$(XS_WOLF_BUILD_ABS) GEN_CSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) GEN_VSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) RTL_DIR=$(XS_WOLF_EMIT_DIR_ABS) SIM_TOP_V=$(XS_WOLF_EMIT_ABS) NUM_CORES=$(XS_NUM_CORES) RTL_SUFFIX=$(XS_RTL_SUFFIX) EMU_THREADS=$(XS_EMU_THREADS) EMU_RANDOMIZE=0 SIM_VFLAGS=\"$(XS_SIM_VFLAGS)\" WITH_CHISELDB=$(XS_WITH_CHISELDB) WITH_CONSTANTIN=$(XS_WITH_CONSTANTIN) SIM_VSRC= $(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,)" | tee -a "$(BUILD_LOG_FILE)"
	$(MAKE) -C $(XS_ROOT)/difftest emu \
		BUILD_DIR=$(XS_WOLF_BUILD_ABS) \
		GEN_CSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) \
		GEN_VSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) \
		RTL_DIR=$(XS_WOLF_EMIT_DIR_ABS) \
		SIM_TOP_V=$(XS_WOLF_EMIT_ABS) \
		NUM_CORES=$(XS_NUM_CORES) \
		RTL_SUFFIX=$(XS_RTL_SUFFIX) \
		EMU_THREADS=$(XS_EMU_THREADS) \
		EMU_RANDOMIZE=0 \
		SIM_VFLAGS="$(XS_SIM_VFLAGS)" \
		WITH_CHISELDB=$(XS_WITH_CHISELDB) \
		WITH_CONSTANTIN=$(XS_WITH_CONSTANTIN) \
		SIM_VSRC= \
		$(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,) \
		2>&1 | tee -a "$(BUILD_LOG_FILE)"

xs_diff_clean:
	rm -rf "$(XS_REF_BUILD_ABS)/verilator-compile" \
		"$(XS_WOLF_BUILD_ABS)/verilator-compile" \
		"$(XS_WOLF_EMIT_DIR_ABS)"

run_xs_ref_emu:
	@RUN_ID="$(if $(RUN_ID),$(RUN_ID),$$(date +%Y%m%d_%H%M%S))"; \
	LOG_DIR="$(XS_LOG_DIR_ABS)"; \
	mkdir -p "$$LOG_DIR"; \
	REF_LOG="$$LOG_DIR/xs_ref_$${RUN_ID}.log"; \
	REF_WAVEFORM="$(XS_WAVEFORM_DIR_ABS)/xs_ref_$${RUN_ID}.fst"; \
	printf '' > "$$REF_LOG"; \
	echo "[RUN] xs ref emu"; \
	echo "[RUN] XS_SIM_MAX_CYCLE=$(XS_SIM_MAX_CYCLE) XS_WAVEFORM=$(XS_WAVEFORM) XS_WAVEFORM_FULL=$(XS_WAVEFORM_FULL)"; \
	echo "[LOG] ref : $$REF_LOG"; \
	if [ "$(XS_WAVEFORM)" = "1" ]; then \
		echo "[WAVEFORM] ref : $$REF_WAVEFORM"; \
	fi; \
	echo "[CMD] cd $(XS_REF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu -i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin --diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so -b 0 $(if $(filter 1,$(XS_WAVEFORM_FULL)),-e -1,-e 0) $(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) $(XS_RAM_TRACE_ARGS) $(if $(filter 1,$(XS_WAVEFORM)),$(if $(filter 1,$(XS_WAVEFORM_FULL)),--dump-wave-full,--dump-wave),) $(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $$REF_WAVEFORM,)"; \
	cd $(XS_REF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu \
		-i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin \
		--diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 $(if $(filter 1,$(XS_WAVEFORM_FULL)),-e -1,-e 0) \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(XS_RAM_TRACE_ARGS) \
		$(if $(filter 1,$(XS_WAVEFORM)),$(if $(filter 1,$(XS_WAVEFORM_FULL)),--dump-wave-full,--dump-wave),) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $$REF_WAVEFORM,) \
		2>&1 | tee "$$REF_LOG"

run_xs_wolf_emu:
	@RUN_ID="$(if $(RUN_ID),$(RUN_ID),$$(date +%Y%m%d_%H%M%S))"; \
	LOG_DIR="$(XS_LOG_DIR_ABS)"; \
	mkdir -p "$$LOG_DIR"; \
	WOLF_LOG="$$LOG_DIR/xs_wolf_$${RUN_ID}.log"; \
	WOLF_WAVEFORM="$(XS_WAVEFORM_DIR_ABS)/xs_wolf_$${RUN_ID}.fst"; \
	printf '' > "$$WOLF_LOG"; \
	echo "[RUN] xs wolf emu"; \
	echo "[RUN] XS_SIM_MAX_CYCLE=$(XS_SIM_MAX_CYCLE) XS_WAVEFORM=$(XS_WAVEFORM) XS_WAVEFORM_FULL=$(XS_WAVEFORM_FULL)"; \
	echo "[LOG] wolf: $$WOLF_LOG"; \
	if [ "$(XS_WAVEFORM)" = "1" ]; then \
		echo "[WAVEFORM] wolf: $$WOLF_WAVEFORM"; \
	fi; \
	echo "[CMD] cd $(XS_WOLF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu -i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin --diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so -b 0 $(if $(filter 1,$(XS_WAVEFORM_FULL)),-e -1,-e 0) $(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) $(XS_RAM_TRACE_ARGS) $(if $(filter 1,$(XS_WAVEFORM)),$(if $(filter 1,$(XS_WAVEFORM_FULL)),--dump-wave-full,--dump-wave),) $(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $$WOLF_WAVEFORM,)"; \
	cd $(XS_WOLF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu \
		-i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin \
		--diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 $(if $(filter 1,$(XS_WAVEFORM_FULL)),-e -1,-e 0) \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(XS_RAM_TRACE_ARGS) \
		$(if $(filter 1,$(XS_WAVEFORM)),$(if $(filter 1,$(XS_WAVEFORM_FULL)),--dump-wave-full,--dump-wave),) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $$WOLF_WAVEFORM,) \
		2>&1 | tee "$$WOLF_LOG"

run_xs_json_test:
	@RUN_ID="$$(date +%Y%m%d_%H%M%S)"; \
	$(MAKE) --no-print-directory xs_wolf_emu RUN_ID=$$RUN_ID XS_JSON_ROUNDTRIP=1; \
	$(MAKE) --no-print-directory run_xs_wolf_emu RUN_ID=$$RUN_ID

run_xs_diff:
	@$(MAKE) --no-print-directory xs_diff_clean
	@$(MAKE) --no-print-directory xs_ref_emu xs_wolf_emu
	@RUN_ID="$$(date +%Y%m%d_%H%M%S)"; \
	echo "[RUN] parallel xs diff"; \
	{ start=$$(date +%s); \
	  $(MAKE) --no-print-directory run_xs_wolf_emu RUN_ID=$$RUN_ID; \
	  wolf_status=$$?; \
	  end=$$(date +%s); \
	  wolf_log="$(XS_LOG_DIR_ABS)/xs_wolf_$${RUN_ID}.log"; \
	  mkdir -p "$(XS_LOG_DIR_ABS)"; \
	  echo "[TIME] xs wolf emu: $$((end-start))s" | tee -a "$$wolf_log"; \
	  exit $$wolf_status; } & wolf_pid=$$!; \
	{ start=$$(date +%s); \
	  $(MAKE) --no-print-directory run_xs_ref_emu RUN_ID=$$RUN_ID; \
	  ref_status=$$?; \
	  end=$$(date +%s); \
	  ref_log="$(XS_LOG_DIR_ABS)/xs_ref_$${RUN_ID}.log"; \
	  mkdir -p "$(XS_LOG_DIR_ABS)"; \
	  echo "[TIME] xs ref emu: $$((end-start))s" | tee -a "$$ref_log"; \
	  exit $$ref_status; } & ref_pid=$$!; \
	wait $$wolf_pid; wolf_status=$$?; \
	wait $$ref_pid; ref_status=$$?; \
	if [ $$wolf_status -ne 0 ] || [ $$ref_status -ne 0 ]; then \
		echo "[FAIL] xs diff: wolf=$$wolf_status ref=$$ref_status"; \
		exit 1; \
	fi

clean:
	rm -rf build/
