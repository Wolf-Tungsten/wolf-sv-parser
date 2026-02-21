SHELL := /bin/bash

# Check env.sh must exist and has been sourced
ENV_FILE := $(CURDIR)/env.sh
ifeq (,$(wildcard $(ENV_FILE)))
    $(error env.sh not found. Please run: cp env.sh.template env.sh && source env.sh)
endif

ifeq ($(WOLF_ENV_SOURCED),)
    $(error env.sh exists but not sourced. Please run: source env.sh)
endif

# Auto-load environment from env.sh
export TOOL_EXTENSION := $(or $(TOOL_EXTENSION),$(shell grep '^export TOOL_EXTENSION=' $(ENV_FILE) 2>/dev/null | cut -d'"' -f2))
export VERILATOR := $(or $(VERILATOR),$(shell grep '^export VERILATOR=' $(ENV_FILE) 2>/dev/null | cut -d'"' -f2))

DUT ?=
CASE ?=
LOG_ONLY_SIM ?= 0

BUILD_DIR ?= build
CMAKE ?= cmake
WOLF_PARSER := $(BUILD_DIR)/bin/wolvrix-cli
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
SKIP_TRANSFORM ?= 0
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
ifneq ($(strip $(SKIP_TRANSFORM)),0)
WOLF_EMIT_FLAGS += --skip-transform
endif
ifneq ($(strip $(WOLF_TIMEOUT)),)
WOLF_EMIT_FLAGS += --timeout $(WOLF_TIMEOUT)
endif

HDLBITS_ROOT := tests/data/hdlbits
C910_ROOT := tests/data/openc910
XS_ROOT := tests/data/xiangshan

# HDLBits paths
DUT_SRC := $(HDLBITS_ROOT)/dut/dut_$(DUT).v
TB_SRC := $(HDLBITS_ROOT)/tb/tb_$(DUT).cpp
OUT_DIR := build/hdlbits/$(DUT)
EMITTED_DUT := $(OUT_DIR)/dut_$(DUT).v
EMITTED_JSON := $(OUT_DIR)/dut_$(DUT).json
SIM_BIN_NAME := sim_$(DUT)
SIM_BIN := $(OUT_DIR)/$(SIM_BIN_NAME)
VERILATOR_PREFIX := Vdut_$(DUT)
VERILATOR_MK := $(OUT_DIR)/$(VERILATOR_PREFIX).mk
TB_SOURCES := $(wildcard $(HDLBITS_ROOT)/tb/tb_*.cpp)
HDLBITS_DUTS := $(sort $(patsubst tb_%,%,$(basename $(notdir $(TB_SOURCES)))))

# C910 paths
C910_SMART_RUN_DIR := $(C910_ROOT)/smart_run
C910_SMART_CODE_BASE ?= $(abspath $(C910_ROOT)/C910_RTL_FACTORY)
SMART_ENV ?= $(C910_SMART_RUN_DIR)/env.sh
SMART_SIM ?= verilator
SMART_CASE ?= coremark
C910_SIM_MAX_CYCLE ?= 0
C910_WAVEFORM ?= 0
C910_LOG_DIR := $(BUILD_DIR)/logs/c910
C910_WAVEFORM_DIR ?= $(C910_LOG_DIR)
C910_LOG_DIR_ABS = $(abspath $(C910_LOG_DIR))
C910_WAVEFORM_DIR_ABS = $(abspath $(C910_WAVEFORM_DIR))
C910_WAVEFORM_PATH_ABS = $(if $(C910_WAVEFORM_PATH),$(if $(filter /%,$(C910_WAVEFORM_PATH)),$(C910_WAVEFORM_PATH),$(abspath $(C910_WAVEFORM_PATH))),)

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

XS_DIFFTEST_GEN_DIR ?= $(XS_ROOT)/build/generated-src
XS_DIFFTEST_GEN_DIR_ABS := $(abspath $(XS_DIFFTEST_GEN_DIR))
XS_WOLF_INCLUDE_DIRS ?= $(XS_RTL_DIR_ABS) $(XS_VSRC_DIR_ABS) $(XS_DIFFTEST_GEN_DIR_ABS)
XS_WOLF_INCLUDE_FLAGS := $(foreach d,$(XS_WOLF_INCLUDE_DIRS),-I $(d))
XS_WOLF_DEFINE_FLAGS := $(foreach d,$(XS_SIM_DEFINES),-D "$(d)")
XS_WOLF_DEFINE_FLAGS_LOG := $(foreach d,$(XS_SIM_DEFINES),-D $(d))

.PHONY: all build check-id run_hdlbits_test run_all_hdlbits_tests \
	run_c910_test xs-rtl xs-wolf-filelist xs-wolf-emit xs-ref-emu xs-wolf-emu \
	xs-diff-clean run_xs_ref_emu run_xs_wolf_emu run_xs_diff clean

all: build

check-id:
	@if [[ ! "$(DUT)" =~ ^[0-9]{3}$$ ]]; then \
		echo "DUT must be a three-digit number (e.g. DUT=001)"; \
		exit 1; \
	fi
	@test -f $(DUT_SRC) || { echo "Missing DUT source: $(DUT_SRC)"; exit 1; }
	@test -f $(TB_SRC) || { echo "Missing testbench: $(TB_SRC)"; exit 1; }

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) 

$(WOLF_PARSER): build

$(EMITTED_DUT) $(EMITTED_JSON): $(DUT_SRC) $(WOLF_PARSER) check-id
	@mkdir -p $(OUT_DIR)
	$(WOLF_PARSER) --emit-sv --emit-json $(WOLF_EMIT_FLAGS) -o $(EMITTED_DUT) $(DUT_SRC)
	@if [ -f $(OUT_DIR)/grh.json ]; then mv -f $(OUT_DIR)/grh.json $(EMITTED_JSON); fi

$(SIM_BIN): $(EMITTED_DUT) $(TB_SRC) check-id
	@mkdir -p $(OUT_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) --cc $(EMITTED_DUT) --exe $(TB_SRC) \
		--top-module top_module --prefix $(VERILATOR_PREFIX) -Mdir $(OUT_DIR) -o $(SIM_BIN_NAME)
	CCACHE_DISABLE=1 $(MAKE) -C $(OUT_DIR) -f $(VERILATOR_PREFIX).mk $(SIM_BIN_NAME)

run_hdlbits_test:
ifneq ($(strip $(DUT)),)
  ifeq ($(DUT),$(filter $(DUT),$(HDLBITS_DUTS)))
	@$(MAKE) --no-print-directory $(SIM_BIN)
	@echo "[RUN] ./$(SIM_BIN)"
	@cd $(OUT_DIR) && ./$(SIM_BIN_NAME)
  else
	$(error DUT=$(DUT) not found; available: $(HDLBITS_DUTS))
  endif
else
	@echo "DUT not set; running all available DUTs: $(HDLBITS_DUTS)"
	@$(MAKE) --no-print-directory run_all_hdlbits_tests
endif

run_all_hdlbits_tests: $(WOLF_PARSER)
	@for dut in $(HDLBITS_DUTS); do \
		echo "==== Running DUT=$$dut ===="; \
		$(MAKE) --no-print-directory run_hdlbits_test DUT=$$dut || exit $$?; \
	done

ifneq ($(strip $(SKIP_WOLF_BUILD)),1)
RUN_C910_TEST_DEPS := build
endif

run_c910_test: $(RUN_C910_TEST_DEPS)
	@CASE_NAME="$(if $(CASE),$(CASE),$(SMART_CASE))"; \
	LOG_FILE="$(if $(LOG_FILE),$(LOG_FILE),$(C910_LOG_DIR)/c910_$${CASE_NAME}_$(shell date +%Y%m%d_%H%M%S).log)"; \
	WAVEFORM_FILE="$(if $(C910_WAVEFORM_PATH_ABS),$(C910_WAVEFORM_PATH_ABS),$(C910_WAVEFORM_DIR_ABS)/c910_$${CASE_NAME}_$(shell date +%Y%m%d_%H%M%S).fst)"; \
	WAVEFORM_DIR="$$(dirname "$$WAVEFORM_FILE")"; \
	mkdir -p "$(C910_LOG_DIR_ABS)" "$$WAVEFORM_DIR"; \
	if [ -z "$(TOOL_EXTENSION)" ] && [ -f "$(SMART_ENV)" ]; then \
		. "$(SMART_ENV)"; \
	fi; \
	echo "[RUN] smart_run CASE=$$CASE_NAME SIM=$(SMART_SIM)"; \
	echo "[RUN] C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM)"; \
	echo "[LOG] Capturing output to: $$LOG_FILE"; \
	if [ "$(C910_WAVEFORM)" = "1" ]; then \
		echo "[WAVEFORM] Will save FST to: $$WAVEFORM_FILE"; \
	fi; \
	if [ "$(LOG_ONLY_SIM)" != "0" ]; then \
		C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) C910_WAVEFORM_PATH="$$WAVEFORM_FILE" \
		$(MAKE) --no-print-directory -C $(C910_SMART_RUN_DIR) runcase \
			CASE=$$CASE_NAME SIM=$(SMART_SIM) \
			C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) \
			CODE_BASE_PATH="$${CODE_BASE_PATH:-$(C910_SMART_CODE_BASE)}" \
			TOOL_EXTENSION="$$TOOL_EXTENSION" \
			VERILATOR="$(VERILATOR)" 2>&1 | \
			tee >(awk 'f{print} index($$0,"obj_dir/Vsim_top"){f=1; next}' > "$$LOG_FILE"); \
	else \
		C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) C910_WAVEFORM_PATH="$$WAVEFORM_FILE" \
		$(MAKE) --no-print-directory -C $(C910_SMART_RUN_DIR) runcase \
			CASE=$$CASE_NAME SIM=$(SMART_SIM) \
			C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) \
			CODE_BASE_PATH="$${CODE_BASE_PATH:-$(C910_SMART_CODE_BASE)}" \
			TOOL_EXTENSION="$$TOOL_EXTENSION" \
			VERILATOR="$(VERILATOR)" 2>&1 | tee "$$LOG_FILE"; \
	fi

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

xs-rtl: $(XS_SIM_TOP_V)

$(XS_WOLF_FILELIST_ABS): $(XS_SIM_TOP_V)
	@mkdir -p "$(dir $@)"
	@{ \
		find "$(XS_RTL_DIR_ABS)" -type f -name "*.sv" -o -type f -name "*.v"; \
		find "$(XS_VSRC_DIR_ABS)" -type f -name "*.sv" -o -type f -name "*.v"; \
	} | LC_ALL=C sort > "$@"

xs-wolf-filelist: $(XS_WOLF_FILELIST_ABS)

ifneq ($(strip $(SKIP_WOLF_BUILD)),1)
XS_WOLF_DEPS := $(WOLF_PARSER)
endif

xs-wolf-emit: $(XS_WOLF_FILELIST_ABS) $(XS_WOLF_DEPS)
	@mkdir -p "$(XS_WOLF_EMIT_DIR_ABS)"
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(RUN_ID))
	@$(eval BUILD_LOG_FILE := $(XS_LOG_DIR_ABS)/xs_wolf_build_$(RUN_ID).log)
	@echo "[LOG] Capturing wolf emit output to: $(BUILD_LOG_FILE)"
	@printf '' > "$(BUILD_LOG_FILE)"
	@echo "[CMD] $(WOLF_PARSER) --emit-sv --top $(XS_SIM_TOP) -o $(XS_WOLF_EMIT_ABS) $(WOLF_EMIT_FLAGS) $(XS_WOLF_EMIT_FLAGS) $(XS_WOLF_INCLUDE_FLAGS) $(XS_WOLF_DEFINE_FLAGS_LOG) -f $(XS_WOLF_FILELIST_ABS)" | tee -a "$(BUILD_LOG_FILE)"
	$(WOLF_PARSER) --emit-sv --top $(XS_SIM_TOP) -o $(XS_WOLF_EMIT_ABS) \
		$(WOLF_EMIT_FLAGS) $(XS_WOLF_EMIT_FLAGS) \
		$(XS_WOLF_INCLUDE_FLAGS) $(XS_WOLF_DEFINE_FLAGS) \
		-f $(XS_WOLF_FILELIST_ABS) \
		2>&1 | tee -a "$(BUILD_LOG_FILE)"

xs-ref-emu: $(XS_SIM_TOP_V)
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

xs-wolf-emu: xs-wolf-emit
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

xs-diff-clean:
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

run_xs_diff:
	@$(MAKE) --no-print-directory xs-diff-clean
	@$(MAKE) --no-print-directory xs-ref-emu xs-wolf-emu
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
