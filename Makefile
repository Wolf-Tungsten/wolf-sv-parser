SHELL := /bin/bash

# Usage: make run_hdlbits_test DUT=001
DUT ?= 001

WOLF_PARSER := build/bin/wolf-sv-parser
HDLBITS_ROOT := tests/data/hdlbits
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

.PHONY: all run_hdlbits_test clean check-id

all: run_hdlbits_test

check-id:
	@if [[ ! "$(DUT)" =~ ^[0-9]{3}$$ ]]; then \
		echo "DUT must be a three-digit number (e.g. DUT=001)"; \
		exit 1; \
	fi
	@test -f $(DUT_SRC) || { echo "Missing DUT source: $(DUT_SRC)"; exit 1; }
	@test -f $(TB_SRC) || { echo "Missing testbench: $(TB_SRC)"; exit 1; }
	@test -x $(WOLF_PARSER) || { echo "Missing parser binary: $(WOLF_PARSER). Build it first."; exit 1; }

$(EMITTED_DUT) $(EMITTED_JSON): $(DUT_SRC) check-id
	@mkdir -p $(OUT_DIR)
	$(WOLF_PARSER) --emit-sv --dump-grh -o $(EMITTED_DUT) $(DUT_SRC)
	@if [ -f $(OUT_DIR)/grh.json ]; then mv -f $(OUT_DIR)/grh.json $(EMITTED_JSON); fi

$(SIM_BIN): $(EMITTED_DUT) $(TB_SRC) check-id
	@mkdir -p $(OUT_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) --cc $(EMITTED_DUT) --exe $(TB_SRC) \
		--top-module top_module --prefix $(VERILATOR_PREFIX) -Mdir $(OUT_DIR) -o $(SIM_BIN_NAME)
	CCACHE_DISABLE=1 $(MAKE) -C $(OUT_DIR) -f $(VERILATOR_PREFIX).mk $(SIM_BIN_NAME)

run_hdlbits_test: $(SIM_BIN)
	@echo "[RUN] ./$(SIM_BIN)"
	@cd $(OUT_DIR) && ./$(SIM_BIN_NAME)

clean:
	rm -rf build/hdlbits
