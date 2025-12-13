SHELL := /bin/bash

# Usage: make run_hdlbits_test DUT=001
DUT ?=
CASE ?=

BUILD_DIR ?= build
CMAKE ?= cmake
WOLF_PARSER := $(BUILD_DIR)/bin/wolf-sv-parser
HDLBITS_ROOT := tests/data/hdlbits
C910_ROOT := tests/data/openc910
C910_PLAN := $(C910_ROOT)/wolf_sv_parser_tb/plan.md
C910_PROJ ?= $(abspath $(C910_ROOT))
VERILATOR ?= verilator
VERILATOR_FLAGS ?= -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNDRIVEN \
	-Wno-SYNCASYNCNET

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

C910_CASE_DIR := $(C910_ROOT)/wolf_sv_parser_tb/case$(CASE)
C910_FILELIST := $(C910_CASE_DIR)/filelist.f
C910_TB := $(C910_CASE_DIR)/tb.cpp
C910_OUT_DIR := $(BUILD_DIR)/c910/case$(CASE)
C910_EXPANDED_FILELIST := $(C910_OUT_DIR)/filelist.f
C910_CASE_DIRS := $(sort $(wildcard $(C910_ROOT)/wolf_sv_parser_tb/case???))
C910_CASES := $(patsubst $(C910_ROOT)/wolf_sv_parser_tb/case%,%,$(C910_CASE_DIRS))
C910_PLAN_INDEX := $(strip $(shell expr 0$(CASE) + 0))
C910_PLAN_ENTRY := $(strip $(shell awk -v n=$(C910_PLAN_INDEX) -F'\\. ' '$$1==n {print $$2}' $(C910_PLAN)))
C910_PLAN_TOP := $(strip $(basename $(notdir $(C910_PLAN_ENTRY))))
C910_TOP ?= $(C910_PLAN_TOP)
C910_EMITTED_DUT := $(C910_OUT_DIR)/$(C910_TOP).v
C910_EMITTED_JSON := $(C910_OUT_DIR)/$(C910_TOP).json
C910_SIM_BIN_NAME := sim_$(CASE)
C910_SIM_BIN := $(C910_OUT_DIR)/$(C910_SIM_BIN_NAME)
C910_VERILATOR_PREFIX := V$(C910_TOP)

.PHONY: all run_hdlbits_test run_c910_test clean check-id c910-check-case

all: run_hdlbits_test

check-id:
	@if [[ ! "$(DUT)" =~ ^[0-9]{3}$$ ]]; then \
		echo "DUT must be a three-digit number (e.g. DUT=001)"; \
		exit 1; \
	fi
	@test -f $(DUT_SRC) || { echo "Missing DUT source: $(DUT_SRC)"; exit 1; }
	@test -f $(TB_SRC) || { echo "Missing testbench: $(TB_SRC)"; exit 1; }

$(WOLF_PARSER): CMakeLists.txt
	$(CMAKE) -S . -B $(BUILD_DIR)
	$(CMAKE) --build $(BUILD_DIR) --target wolf-sv-parser

$(EMITTED_DUT) $(EMITTED_JSON): $(DUT_SRC) $(WOLF_PARSER) check-id
	@mkdir -p $(OUT_DIR)
	$(WOLF_PARSER) --emit-sv --emit-json -o $(EMITTED_DUT) $(DUT_SRC)
	@if [ -f $(OUT_DIR)/grh.json ]; then mv -f $(OUT_DIR)/grh.json $(EMITTED_JSON); fi

$(SIM_BIN): $(EMITTED_DUT) $(TB_SRC) check-id
	@mkdir -p $(OUT_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) --cc $(EMITTED_DUT) --exe $(TB_SRC) \
		--top-module top_module --prefix $(VERILATOR_PREFIX) -Mdir $(OUT_DIR) -o $(SIM_BIN_NAME)
	CCACHE_DISABLE=1 $(MAKE) -C $(OUT_DIR) -f $(VERILATOR_PREFIX).mk $(SIM_BIN_NAME)

c910-check-case:
	@if [[ -z "$(CASE)" ]]; then echo "CASE must be set (e.g. CASE=001)"; exit 1; fi
	@if [[ ! "$(CASE)" =~ ^[0-9]{3}$$ ]]; then \
		echo "CASE must be a three-digit number (e.g. CASE=001)"; \
		exit 1; \
	fi
	@test -f $(C910_PLAN) || { echo "Missing plan file: $(C910_PLAN)"; exit 1; }
	@if [[ -z "$(C910_PLAN_ENTRY)" ]]; then \
		echo "CASE $(CASE) not found in $(C910_PLAN)"; \
		exit 1; \
	fi
	@if [[ -z "$(C910_TOP)" ]]; then \
		echo "Failed to determine top module for CASE=$(CASE)"; \
		exit 1; \
	fi
	@test -f $(C910_FILELIST) || { echo "Missing filelist: $(C910_FILELIST)"; exit 1; }
	@test -f $(C910_TB) || { echo "Missing testbench: $(C910_TB)"; exit 1; }

$(C910_EXPANDED_FILELIST): $(C910_FILELIST) c910-check-case
	@mkdir -p $(C910_OUT_DIR)
	@C910_PROJ=$(C910_PROJ) envsubst < $(C910_FILELIST) > $(C910_EXPANDED_FILELIST)

$(C910_EMITTED_DUT): $(C910_EXPANDED_FILELIST) $(WOLF_PARSER) c910-check-case
	$(WOLF_PARSER) --emit-sv --emit-json --emit-out $(C910_OUT_DIR) --top $(C910_TOP) -o $(C910_EMITTED_DUT) -f $(C910_EXPANDED_FILELIST)
	@if [ -f $(C910_OUT_DIR)/grh.json ] && [ ! -f $(C910_EMITTED_JSON) ]; then mv -f $(C910_OUT_DIR)/grh.json $(C910_EMITTED_JSON); fi

$(C910_SIM_BIN): $(C910_EMITTED_DUT) $(C910_TB) c910-check-case
	@mkdir -p $(C910_OUT_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) --coverage --cc $(C910_EMITTED_DUT) --exe $(C910_TB) \
		--top-module $(C910_TOP) -Mdir $(C910_OUT_DIR) -o $(C910_SIM_BIN_NAME) \
		-CFLAGS "-std=c++17"
	CCACHE_DISABLE=1 VERILATOR_NO_CCACHE=1 $(MAKE) -C $(C910_OUT_DIR) -f $(C910_VERILATOR_PREFIX).mk $(C910_SIM_BIN_NAME)

run_c910_test:
ifneq ($(strip $(CASE)),)
	@$(MAKE) --no-print-directory $(C910_SIM_BIN)
	@echo "[RUN] $(C910_SIM_BIN)"
	@COV_OUT=$(C910_OUT_DIR)/coverage.dat $(C910_SIM_BIN)
else
	@if [ -z "$(C910_CASES)" ]; then echo "No C910 cases found under $(C910_ROOT)/wolf_sv_parser_tb"; exit 1; fi
	@echo "CASE not set; running all available C910 cases: $(C910_CASES)"
	@for case in $(C910_CASES); do \
		echo "==== Running CASE=$$case ===="; \
		$(MAKE) --no-print-directory run_c910_test CASE=$$case || exit $$?; \
	done
endif

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
