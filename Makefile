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
C910_SMART_CODE_BASE ?= $(abspath $(C910_ROOT)/C910_RTL_FACTORY)
SMART_SIM ?= verilator
SMART_CASE ?= coremark
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

.PHONY: all run_hdlbits_test run_c910_test clean check-id

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

run_c910_test:
	@CASE_NAME="$(if $(CASE),$(CASE),$(SMART_CASE))"; \
	echo "[RUN] smart_run CASE=$$CASE_NAME SIM=$(SMART_SIM)"; \
	$(MAKE) --no-print-directory -C $(C910_SMART_RUN_DIR) runcase \
		CASE=$$CASE_NAME SIM=$(SMART_SIM) \
		CODE_BASE_PATH="$(C910_SMART_CODE_BASE)"

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
