SHELL := /bin/bash

# Check env.sh must exist and has been sourced
ENV_FILE := $(CURDIR)/env.sh
ifeq (,$(wildcard $(ENV_FILE)))
    $(error env.sh not found. Please run: cp env.sh.template env.sh && source env.sh)
endif

# Guard: check if env.sh has been sourced (WOLF_ENV_SOURCED should be set)
ifeq ($(WOLF_ENV_SOURCED),)
    $(error env.sh exists but not sourced. Please run: source env.sh)
endif

# Auto-load environment from env.sh (user-specific config)
# This allows users to configure TOOL_EXTENSION, VERILATOR, etc. without manual sourcing
export TOOL_EXTENSION := $(or $(TOOL_EXTENSION),$(shell grep '^export TOOL_EXTENSION=' $(ENV_FILE) 2>/dev/null | cut -d'"' -f2))
export VERILATOR := $(or $(VERILATOR),$(shell grep '^export VERILATOR=' $(ENV_FILE) 2>/dev/null | cut -d'"' -f2))

# Usage: make run_hdlbits_test DUT=001
DUT ?=
CASE ?=

BUILD_DIR ?= build
CMAKE ?= cmake
WOLF_PARSER := $(BUILD_DIR)/bin/wolf-sv-parser
RUN_ID ?= $(shell date +%Y%m%d_%H%M%S)
RUN_ID := $(RUN_ID)
CCACHE_DIR ?= $(BUILD_DIR)/ccache
export CCACHE_DIR
$(shell mkdir -p "$(CCACHE_DIR)")
VM_PARALLEL_BUILDS ?= 1
export VM_PARALLEL_BUILDS
HDLBITS_ROOT := tests/data/hdlbits
C910_ROOT := tests/data/openc910
XS_ROOT := tests/data/xiangshan
C910_SMART_RUN_DIR := $(C910_ROOT)/smart_run
C910_BUG_CASES_DIR := $(C910_ROOT)/bug_cases
C910_SMART_CODE_BASE ?= $(abspath $(C910_ROOT)/C910_RTL_FACTORY)
SMART_ENV ?= $(C910_SMART_RUN_DIR)/env.sh
SMART_SIM ?= verilator
SMART_CASE ?= coremark
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
CMAKE_BUILD_TYPE ?= Release

# C910 simulation control
C910_SIM_MAX_CYCLE ?= 0
C910_WAVEFORM ?= 0
# Waveform output control
# - C910_WAVEFORM_PATH: explicit FST file path (relative paths resolve from repo root)
# - C910_WAVEFORM_DIR: directory for auto-generated FST filenames (default: C910_LOG_DIR)
C910_WAVEFORM_DIR ?= $(C910_LOG_DIR)
C910_LOG_DIR_ABS = $(abspath $(C910_LOG_DIR))
C910_WAVEFORM_DIR_ABS = $(abspath $(C910_WAVEFORM_DIR))
C910_WAVEFORM_PATH_ABS = $(if $(C910_WAVEFORM_PATH),$(if $(filter /%,$(C910_WAVEFORM_PATH)),$(C910_WAVEFORM_PATH),$(abspath $(C910_WAVEFORM_PATH))),)

# XiangShan simulation control
XS_SIM_MAX_CYCLE ?= 50000
XS_WAVEFORM ?= 1
XS_WAVEFORM_FULL ?= 1
# XiangShan emu build control
XS_NUM_CORES ?= 1
XS_EMU_THREADS ?= 4
XS_SIM_TOP ?= SimTop
XS_RTL_SUFFIX ?= sv
XS_WITH_CHISELDB ?= 0
XS_WITH_CONSTANTIN ?= 0
XS_EMU_PREFIX ?= $(shell if command -v stdbuf >/dev/null 2>&1; then echo "stdbuf -oL -eL"; fi)
# Waveform output control
# - XS_WAVEFORM_FULL: dump full waveform range (default: 1)
# - XS_WAVEFORM_PATH: explicit FST file path (relative paths resolve from repo root)
# - XS_WAVEFORM_DIR: directory for auto-generated FST filenames (default: XS_LOG_DIR)
XS_LOG_DIR := $(BUILD_DIR)/logs/xs
XS_WAVEFORM_DIR ?= $(XS_LOG_DIR)
XS_LOG_DIR_ABS = $(abspath $(XS_LOG_DIR))
XS_WAVEFORM_DIR_ABS = $(abspath $(XS_WAVEFORM_DIR))
XS_WAVEFORM_PATH_ABS = $(if $(XS_WAVEFORM_PATH),$(if $(filter /%,$(XS_WAVEFORM_PATH)),$(XS_WAVEFORM_PATH),$(abspath $(XS_WAVEFORM_PATH))),)

# XiangShan wolf integration paths
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
XS_WOLF_DEFINE_FLAGS := $(foreach d,$(XS_SIM_DEFINES),-D $(d))

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
C910_BUG_CASE_DIRS := $(wildcard $(C910_BUG_CASES_DIR)/case_*)
C910_BUG_CASES := $(sort $(notdir $(C910_BUG_CASE_DIRS)))

.PHONY: all run_hdlbits_test run_c910_test run_c910_ref_test run_c910_diff build_wolf_parser \
	run_c910_bug_case run_c910_bug_case_ref run_c910_all_bug_case run_c910_gprof \
	run_c910_prof clean check-id xs-init xs-clean run_xs_ref_test \
	xs-rtl xs-wolf-filelist xs-wolf-emit xs-ref-emu xs-wolf-emu run_xs_test run_xs_diff

all: build_wolf_parser

check-id:
	@if [[ ! "$(DUT)" =~ ^[0-9]{3}$$ ]]; then \
		echo "DUT must be a three-digit number (e.g. DUT=001)"; \
		exit 1; \
	fi
	@test -f $(DUT_SRC) || { echo "Missing DUT source: $(DUT_SRC)"; exit 1; }
	@test -f $(TB_SRC) || { echo "Missing testbench: $(TB_SRC)"; exit 1; }

build_wolf_parser:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) --target wolf-sv-parser

$(WOLF_PARSER): build_wolf_parser

$(EMITTED_DUT) $(EMITTED_JSON): $(DUT_SRC) $(WOLF_PARSER) check-id
	@mkdir -p $(OUT_DIR)
	$(WOLF_PARSER) --emit-sv --emit-json $(WOLF_EMIT_FLAGS) -o $(EMITTED_DUT) $(DUT_SRC)
	@if [ -f $(OUT_DIR)/grh.json ]; then mv -f $(OUT_DIR)/grh.json $(EMITTED_JSON); fi

$(SIM_BIN): $(EMITTED_DUT) $(TB_SRC) check-id
	@mkdir -p $(OUT_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) --cc $(EMITTED_DUT) --exe $(TB_SRC) \
		--top-module top_module --prefix $(VERILATOR_PREFIX) -Mdir $(OUT_DIR) -o $(SIM_BIN_NAME)
	CCACHE_DISABLE=1 $(MAKE) -C $(OUT_DIR) -f $(VERILATOR_PREFIX).mk $(SIM_BIN_NAME)

C910_LOG_DIR := $(BUILD_DIR)/logs/c910
C910_DIFF_WORK_BASE ?= $(C910_SMART_RUN_DIR)
C910_DIFF_WOLF_WORK_DIR ?= $(abspath $(C910_DIFF_WORK_BASE)/work_wolf)
C910_DIFF_REF_WORK_DIR ?= $(abspath $(C910_DIFF_WORK_BASE)/work_ref)

ifneq ($(strip $(SKIP_WOLF_BUILD)),1)
RUN_C910_TEST_DEPS := build_wolf_parser
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

run_c910_diff: build_wolf_parser
	@CASE_NAME="$(if $(CASE),$(CASE),$(SMART_CASE))"; \
	RUN_ID="$$(date +%Y%m%d_%H%M%S)"; \
	LOG_DIR="$(C910_LOG_DIR_ABS)"; \
	WOLF_WORK_DIR="$(C910_DIFF_WOLF_WORK_DIR)"; \
	REF_WORK_DIR="$(C910_DIFF_REF_WORK_DIR)"; \
	mkdir -p "$$LOG_DIR" "$$WOLF_WORK_DIR" "$$REF_WORK_DIR"; \
	WOLF_LOG="$$LOG_DIR/c910_wolf_$${CASE_NAME}_$${RUN_ID}.log"; \
	REF_LOG="$$LOG_DIR/c910_ref_$${CASE_NAME}_$${RUN_ID}.log"; \
	WOLF_WAVEFORM="$(C910_WAVEFORM_DIR_ABS)/c910_wolf_$${CASE_NAME}_$${RUN_ID}.fst"; \
	REF_WAVEFORM="$(C910_WAVEFORM_DIR_ABS)/c910_ref_$${CASE_NAME}_$${RUN_ID}.fst"; \
	echo "[RUN] parallel c910 diff CASE=$$CASE_NAME"; \
	echo "[RUN] C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM)"; \
	echo "[LOG] wolf: $$WOLF_LOG"; \
	echo "[LOG] ref : $$REF_LOG"; \
	if [ "$(C910_WAVEFORM)" = "1" ]; then \
		echo "[WAVEFORM] wolf: $$WOLF_WAVEFORM"; \
		echo "[WAVEFORM] ref : $$REF_WAVEFORM"; \
	fi; \
	$(MAKE) --no-print-directory run_c910_test CASE=$$CASE_NAME \
		WORK_DIR="$$WOLF_WORK_DIR" CCACHE_DIR="$$WOLF_WORK_DIR/.ccache" \
		LOG_FILE="$$WOLF_LOG" LOG_ONLY_SIM=1 SKIP_WOLF_BUILD=1 \
		C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) \
		C910_WAVEFORM_PATH="$$WOLF_WAVEFORM" & \
	wolf_pid=$$!; \
	$(MAKE) --no-print-directory run_c910_ref_test CASE=$$CASE_NAME \
		WORK_DIR="$$REF_WORK_DIR" CCACHE_DIR="$$REF_WORK_DIR/.ccache" \
		LOG_FILE="$$REF_LOG" LOG_ONLY_SIM=1 SKIP_WOLF_BUILD=1 \
		C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) \
		C910_WAVEFORM_PATH="$$REF_WAVEFORM" & \
	ref_pid=$$!; \
	wait $$wolf_pid; wolf_status=$$?; \
	wait $$ref_pid; ref_status=$$?; \
	if [ $$wolf_status -ne 0 ] || [ $$ref_status -ne 0 ]; then \
		echo "[FAIL] c910 diff: wolf=$$wolf_status ref=$$ref_status"; \
		exit 1; \
	fi

TIMEOUT ?= 120
GPROF_DIR := $(BUILD_DIR)/artifacts
GPROF_OUT := $(GPROF_DIR)/c910_gprof.txt
GPROF_GMON := $(GPROF_DIR)/c910_gmon.out

run_c910_gprof: WOLF_TIMEOUT := $(TIMEOUT)
run_c910_gprof:
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-pg
	@$(CMAKE) --build $(BUILD_DIR) -j$(nproc) --target wolf-sv-parser
	@mkdir -p $(GPROF_DIR)
	@rm -f $(GPROF_GMON) $(GPROF_OUT)
	@if [ -z "$(TOOL_EXTENSION)" ] && [ -f "$(SMART_ENV)" ]; then \
		. "$(SMART_ENV)"; \
	fi; \
	echo "[RUN] wolf-sv-parser gprof TIMEOUT=$(TIMEOUT)s"; \
	$(MAKE) --no-print-directory -C $(C910_SMART_RUN_DIR) wolf_emit \
		CODE_BASE_PATH="$${CODE_BASE_PATH:-$(C910_SMART_CODE_BASE)}" \
		TOOL_EXTENSION="$$TOOL_EXTENSION" \
		VERILATOR="$(VERILATOR)" \
		WOLF_SV_PARSER="$(abspath $(WOLF_PARSER))" \
		WOLF_EMIT_FLAGS="$(WOLF_EMIT_FLAGS) --emit-sv --top $${WOLF_TOP:-sim_top} \
			--log info --profile-timer"; \
	if [ -f "$(C910_SMART_RUN_DIR)/work/gmon.out" ]; then \
		cp -f "$(C910_SMART_RUN_DIR)/work/gmon.out" "$(GPROF_GMON)"; \
		gprof "$(WOLF_PARSER)" "$(GPROF_GMON)" > "$(GPROF_OUT)"; \
		echo "[gprof] report: $(GPROF_OUT)"; \
	else \
		echo "[gprof] gmon.out not found; wolf-sv-parser may not have produced profile output"; \
		exit 1; \
	fi

run_c910_ref_test: SMART_SIM=verilator_ref
run_c910_ref_test: run_c910_test

ifneq ($(strip $(SKIP_WOLF_BUILD)),1)
C910_BUG_CASE_DEPS := build_wolf_parser
endif

run_c910_bug_case: $(C910_BUG_CASE_DEPS)
	@case_name="$(CASE)"; \
	if [ -z "$$case_name" ]; then \
		echo "CASE not set; use CASE=case_XXX (available: $(C910_BUG_CASES))"; \
		exit 1; \
	fi; \
	if [[ "$$case_name" != case_* ]]; then \
		case_name="case_$$case_name"; \
	fi; \
	if [ ! -d "$(C910_BUG_CASES_DIR)/$$case_name" ]; then \
		echo "CASE not found: $$case_name"; \
		echo "Available: $(C910_BUG_CASES)"; \
		exit 1; \
	fi; \
	$(MAKE) --no-print-directory -C $(C910_BUG_CASES_DIR)/$$case_name clean; \
	$(MAKE) --no-print-directory -C $(C910_BUG_CASES_DIR)/$$case_name \
		COVERAGE=1 VERILATOR="$(VERILATOR)" run_c910_bug_case

run_c910_bug_case_ref:
	@case_name="$(CASE)"; \
	if [ -z "$$case_name" ]; then \
		echo "CASE not set; use CASE=case_XXX (available: $(C910_BUG_CASES))"; \
		exit 1; \
	fi; \
	if [[ "$$case_name" != case_* ]]; then \
		case_name="case_$$case_name"; \
	fi; \
	if [ ! -d "$(C910_BUG_CASES_DIR)/$$case_name" ]; then \
		echo "CASE not found: $$case_name"; \
		echo "Available: $(C910_BUG_CASES)"; \
		exit 1; \
	fi; \
	$(MAKE) --no-print-directory -C $(C910_BUG_CASES_DIR)/$$case_name clean; \
	$(MAKE) --no-print-directory -C $(C910_BUG_CASES_DIR)/$$case_name \
		COVERAGE=1 VERILATOR="$(VERILATOR)" run_c910_bug_case_ref

ifneq ($(strip $(SKIP_WOLF_BUILD)),1)
C910_BUG_CASE_ALL_DEPS := build_wolf_parser
endif

run_c910_all_bug_case: $(C910_BUG_CASE_ALL_DEPS)
	@for bug_case in $(C910_BUG_CASES); do \
		echo "==== Running CASE=$$bug_case ===="; \
		$(MAKE) --no-print-directory run_c910_bug_case CASE=$$bug_case \
			SKIP_WOLF_BUILD=1 || exit $$?; \
	done

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

.PHONY: run_all_hdlbits_tests
run_all_hdlbits_tests: $(WOLF_PARSER)
	@for dut in $(HDLBITS_DUTS); do \
		echo "==== Running DUT=$$dut ===="; \
		$(MAKE) --no-print-directory run_hdlbits_test DUT=$$dut || exit $$?; \
	done

clean:
	rm -rf build/

# XiangShan targets
xs-init:
	$(MAKE) -C $(XS_ROOT) init

xs-clean:
	$(MAKE) -C $(XS_ROOT) clean

run_xs_ref_test:
	@echo "[RUN] Building XiangShan emu with EMU_THREADS=$(XS_EMU_THREADS)..."
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(if $(RUN_ID),$(RUN_ID),$(shell date +%Y%m%d_%H%M%S)))
	@$(eval BUILD_LOG_FILE := $(XS_LOG_DIR_ABS)/xs_ref_build_$(RUN_ID).log)
	@$(eval LOG_FILE := $(XS_LOG_DIR_ABS)/xs_ref_$(RUN_ID).log)
	@$(eval WAVEFORM_FILE := $(if $(XS_WAVEFORM_PATH_ABS),$(XS_WAVEFORM_PATH_ABS),$(XS_WAVEFORM_DIR_ABS)/xs_ref_$(RUN_ID).fst))
	@echo "[LOG] Capturing build output to: $(BUILD_LOG_FILE)"
	@printf '' > "$(BUILD_LOG_FILE)"
	$(MAKE) -C $(XS_ROOT) emu \
		EMU_THREADS=$(XS_EMU_THREADS) \
		EMU_RANDOMIZE=0 \
		NUM_CORES=1 \
		$(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,) \
		2>&1 | tee "$(BUILD_LOG_FILE)"
	@echo "[RUN] Running XiangShan emu..."
	@echo "[RUN] XS_SIM_MAX_CYCLE=$(XS_SIM_MAX_CYCLE) XS_WAVEFORM=$(XS_WAVEFORM)"
	@echo "[LOG] Capturing output to: $(LOG_FILE)"
	@printf '' > "$(LOG_FILE)"
	$(if $(filter 1,$(XS_WAVEFORM)),@echo "[WAVEFORM] Will save FST to: $(WAVEFORM_FILE)",)
	cd $(XS_ROOT) && $(XS_EMU_PREFIX) ./build/emu \
		-i ./ready-to-run/coremark-2-iteration.bin \
		--diff ./ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 -e 0 \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(if $(filter 1,$(XS_WAVEFORM)),--dump-wave,) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $(WAVEFORM_FILE),) \
		2>&1 | tee "$(LOG_FILE)"

$(XS_SIM_TOP_V):
	@echo "[RUN] Generating XiangShan sim-verilog into $(XS_RTL_BUILD_ABS)..."
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
	$(MAKE) -C $(XS_ROOT)/difftest emu \
		BUILD_DIR=$(XS_WOLF_BUILD_ABS) \
		GEN_CSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) \
		GEN_VSRC_DIR=$(XS_DIFFTEST_GEN_DIR_ABS) \
		RTL_DIR=$(XS_WOLF_EMIT_DIR_ABS) \
		SIM_TOP_V=$(XS_WOLF_EMIT_ABS) \
		NUM_CORES=$(XS_NUM_CORES) \
		RTL_SUFFIX=$(XS_RTL_SUFFIX) \
		EMU_THREADS=$(XS_EMU_THREADS) \
		WITH_CHISELDB=$(XS_WITH_CHISELDB) \
		WITH_CONSTANTIN=$(XS_WITH_CONSTANTIN) \
		SIM_VSRC= \
		$(if $(filter 1,$(XS_WAVEFORM)),EMU_TRACE=fst,) \
		2>&1 | tee -a "$(BUILD_LOG_FILE)"

xs-diff-clean:
	rm -rf "$(XS_REF_BUILD_ABS)/verilator-compile" \
		"$(XS_WOLF_BUILD_ABS)/verilator-compile" \
		"$(XS_WOLF_EMIT_DIR_ABS)" \
		"$(XS_LOG_DIR_ABS)"

run_xs_test: xs-wolf-emu
	@echo "[RUN] Running XiangShan wolf emu..."
	@mkdir -p "$(XS_LOG_DIR_ABS)"
	@$(eval RUN_ID := $(if $(RUN_ID),$(RUN_ID),$(shell date +%Y%m%d_%H%M%S)))
	@$(eval LOG_FILE := $(XS_LOG_DIR_ABS)/xs_wolf_$(RUN_ID).log)
	@$(eval WAVEFORM_FILE := $(if $(XS_WAVEFORM_PATH_ABS),$(XS_WAVEFORM_PATH_ABS),$(XS_WAVEFORM_DIR_ABS)/xs_wolf_$(RUN_ID).fst))
	@echo "[RUN] XS_SIM_MAX_CYCLE=$(XS_SIM_MAX_CYCLE) XS_WAVEFORM=$(XS_WAVEFORM)"
	@echo "[LOG] Capturing output to: $(LOG_FILE)"
	@printf '' > "$(LOG_FILE)"
	$(if $(filter 1,$(XS_WAVEFORM)),@echo "[WAVEFORM] Will save FST to: $(WAVEFORM_FILE)",)
	cd $(XS_WOLF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu \
		-i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin \
		--diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 -e 0 \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(if $(filter 1,$(XS_WAVEFORM)),--dump-wave,) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $(WAVEFORM_FILE),) \
		2>&1 | tee "$(LOG_FILE)"

run_xs_diff:
	@$(MAKE) --no-print-directory xs-diff-clean
	@$(MAKE) --no-print-directory xs-ref-emu xs-wolf-emu
	@RUN_ID="$$(date +%Y%m%d_%H%M%S)"; \
	LOG_DIR="$(XS_LOG_DIR_ABS)"; \
	mkdir -p "$$LOG_DIR"; \
	WOLF_LOG="$$LOG_DIR/xs_wolf_$${RUN_ID}.log"; \
	REF_LOG="$$LOG_DIR/xs_ref_$${RUN_ID}.log"; \
	WOLF_WAVEFORM="$(XS_WAVEFORM_DIR_ABS)/xs_wolf_$${RUN_ID}.fst"; \
	REF_WAVEFORM="$(XS_WAVEFORM_DIR_ABS)/xs_ref_$${RUN_ID}.fst"; \
	printf '' > "$$WOLF_LOG"; \
	printf '' > "$$REF_LOG"; \
	echo "[RUN] parallel xs diff"; \
	echo "[RUN] XS_SIM_MAX_CYCLE=$(XS_SIM_MAX_CYCLE) XS_WAVEFORM=$(XS_WAVEFORM) XS_WAVEFORM_FULL=$(XS_WAVEFORM_FULL)"; \
	echo "[LOG] wolf: $$WOLF_LOG"; \
	echo "[LOG] ref : $$REF_LOG"; \
	if [ "$(XS_WAVEFORM)" = "1" ]; then \
		echo "[WAVEFORM] wolf: $$WOLF_WAVEFORM"; \
		echo "[WAVEFORM] ref : $$REF_WAVEFORM"; \
	fi; \
	cd $(XS_WOLF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu \
		-i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin \
		--diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 $(if $(filter 1,$(XS_WAVEFORM_FULL)),-e -1,-e 0) \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(if $(filter 1,$(XS_WAVEFORM)),$(if $(filter 1,$(XS_WAVEFORM_FULL)),--dump-wave-full,--dump-wave),) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $$WOLF_WAVEFORM,) \
		2>&1 | tee "$$WOLF_LOG" & \
	wolf_pid=$$!; \
	cd $(XS_REF_BUILD_ABS) && $(XS_EMU_PREFIX) ./emu \
		-i $(XS_ROOT_ABS)/ready-to-run/coremark-2-iteration.bin \
		--diff $(XS_ROOT_ABS)/ready-to-run/riscv64-nemu-interpreter-so \
		-b 0 $(if $(filter 1,$(XS_WAVEFORM_FULL)),-e -1,-e 0) \
		$(if $(filter-out 0,$(XS_SIM_MAX_CYCLE)),-C $(XS_SIM_MAX_CYCLE),) \
		$(if $(filter 1,$(XS_WAVEFORM)),$(if $(filter 1,$(XS_WAVEFORM_FULL)),--dump-wave-full,--dump-wave),) \
		$(if $(filter 1,$(XS_WAVEFORM))$(XS_WAVEFORM_PATH),--wave-path $$REF_WAVEFORM,) \
		2>&1 | tee "$$REF_LOG" & \
	ref_pid=$$!; \
	wait $$wolf_pid; wolf_status=$$?; \
	wait $$ref_pid; ref_status=$$?; \
	if [ $$wolf_status -ne 0 ] || [ $$ref_status -ne 0 ]; then \
		echo "[FAIL] xs diff: wolf=$$wolf_status ref=$$ref_status"; \
		exit 1; \
	fi
