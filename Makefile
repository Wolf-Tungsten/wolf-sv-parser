SHELL := /bin/bash

# Usage: make run_hdlbits_test DUT=001
DUT ?=
CASE ?=

BUILD_DIR ?= build
CMAKE ?= cmake
WOLF_PARSER := $(BUILD_DIR)/bin/wolf-sv-parser
HDLBITS_ROOT := tests/data/hdlbits
C910_ROOT := tests/data/openc910
C910_SMART_RUN_DIR := $(C910_ROOT)/smart_run
C910_BUG_CASES_DIR := $(C910_ROOT)/bug_cases
C910_SMART_CODE_BASE ?= $(abspath $(C910_ROOT)/C910_RTL_FACTORY)
SMART_ENV ?= $(C910_SMART_RUN_DIR)/env.sh
SMART_SIM ?= verilator
SMART_CASE ?= coremark
VERILATOR ?= verilator
VERILATOR_FLAGS ?= -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNDRIVEN \
	-Wno-SYNCASYNCNET
SINGLE_THREAD ?= 0
CONVERT_LOG ?= 0
CONVERT_LOG_LEVEL ?= info
CONVERT_LOG_TAG ?= timing
SKIP_TRANSFORM ?= 0
WOLF_TIMEOUT ?= 60
WOLF_EMIT_FLAGS ?=
CMAKE_BUILD_TYPE ?= Release

# C910 simulation control
C910_SIM_MAX_CYCLE ?= 5000
C910_WAVEFORM ?= 0

ifneq ($(strip $(SINGLE_THREAD)),0)
WOLF_EMIT_FLAGS += --single-thread
endif
ifneq ($(strip $(CONVERT_LOG)),0)
WOLF_EMIT_FLAGS += --convert-log --convert-log-level $(CONVERT_LOG_LEVEL) --convert-log-tag $(CONVERT_LOG_TAG)
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
	run_c910_prof clean check-id

all: run_hdlbits_test

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
	WAVEFORM_FILE="$(if $(C910_WAVEFORM_PATH),$(C910_WAVEFORM_PATH),$(C910_LOG_DIR)/c910_$${CASE_NAME}_$(shell date +%Y%m%d_%H%M%S).fst)"; \
	mkdir -p "$(C910_LOG_DIR)"; \
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
			TOOL_EXTENSION="$$TOOL_EXTENSION" 2>&1 | \
			tee >(awk 'f{print} index($$0,"obj_dir/Vsim_top"){f=1; next}' > "$$LOG_FILE"); \
	else \
		C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) C910_WAVEFORM_PATH="$$WAVEFORM_FILE" \
		$(MAKE) --no-print-directory -C $(C910_SMART_RUN_DIR) runcase \
			CASE=$$CASE_NAME SIM=$(SMART_SIM) \
			C910_SIM_MAX_CYCLE=$(C910_SIM_MAX_CYCLE) C910_WAVEFORM=$(C910_WAVEFORM) \
			CODE_BASE_PATH="$${CODE_BASE_PATH:-$(C910_SMART_CODE_BASE)}" \
			TOOL_EXTENSION="$$TOOL_EXTENSION" 2>&1 | tee "$$LOG_FILE"; \
	fi

run_c910_diff: build_wolf_parser
	@CASE_NAME="$(if $(CASE),$(CASE),$(SMART_CASE))"; \
	RUN_ID="$$(date +%Y%m%d_%H%M%S)"; \
	LOG_DIR="$(C910_LOG_DIR)"; \
	WOLF_WORK_DIR="$(C910_DIFF_WOLF_WORK_DIR)"; \
	REF_WORK_DIR="$(C910_DIFF_REF_WORK_DIR)"; \
	mkdir -p "$$LOG_DIR" "$$WOLF_WORK_DIR" "$$REF_WORK_DIR"; \
	WOLF_LOG="$$LOG_DIR/c910_wolf_$${CASE_NAME}_$${RUN_ID}.log"; \
	REF_LOG="$$LOG_DIR/c910_ref_$${CASE_NAME}_$${RUN_ID}.log"; \
	WOLF_WAVEFORM="$$LOG_DIR/c910_wolf_$${CASE_NAME}_$${RUN_ID}.fst"; \
	REF_WAVEFORM="$$LOG_DIR/c910_ref_$${CASE_NAME}_$${RUN_ID}.fst"; \
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
		WOLF_SV_PARSER="$(abspath $(WOLF_PARSER))" \
		WOLF_EMIT_FLAGS="$(WOLF_EMIT_FLAGS) --emit-sv --top $${WOLF_TOP:-sim_top} \
			--convert-log --convert-log-level info --convert-log-tag timing"; \
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
		COVERAGE=1 run_c910_bug_case

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
		COVERAGE=1 run_c910_bug_case_ref

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
