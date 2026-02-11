ROOT := $(abspath ../../../..)

CASE_ID := 001
CASE_TAG := CASE_$(CASE_ID)

DUT_TOP := sram_array_1p128x76m1s1h0l1b_bpu_ittage_bank1
TB_TOP := xs_bugcase_tb

CASE_DIR := $(CURDIR)
FILELIST := $(CASE_DIR)/filelist.f
TB_V := $(CASE_DIR)/tb.v
TB_CPP := $(CASE_DIR)/tb.cpp

OUT_DIR_BASE := $(ROOT)/build/xs_bugcase/$(CASE_TAG)
REF_DIR := $(OUT_DIR_BASE)/ref
WOLF_DIR := $(OUT_DIR_BASE)/wolf

WOLF_SV_PARSER ?= $(ROOT)/build/bin/wolf-sv-parser
WOLF_SV := $(WOLF_DIR)/wolf_emit.sv

VERILATOR ?= $(or $(shell echo $$VERILATOR),verilator)
VERILATOR_COVERAGE ?= verilator_coverage
VERILATOR_ROOT ?= $(shell $(VERILATOR) -V 2>/dev/null | awk -F'= *' '/^VERILATOR_ROOT/ {print $$2}')
VERILATOR_COV_SRC = $(if $(filter 1,$(COVERAGE)),$(VERILATOR_ROOT)/include/verilated_cov.cpp,)

COVERAGE ?= 1
COV_MIN ?= 90
COV_DATA := $(OUT_DIR_BASE)/coverage.dat
COV_INFO := $(OUT_DIR_BASE)/coverage.info
COV_CHECK := $(CASE_DIR)/coverage_check.py

BASE_FLAGS := --no-timing -Wall -Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL \
	-Wno-UNDRIVEN -Wno-SYNCASYNCNET -Wno-PINCONNECTEMPTY -Wno-ASSIGNDLY -Wno-MULTIDRIVEN
REF_FLAGS := $(BASE_FLAGS) $(if $(filter 1,$(COVERAGE)),--coverage,)
WOLF_FLAGS := $(BASE_FLAGS)

REF_PREFIX := VRef
WOLF_PREFIX := VWolf
REF_LIB := $(REF_DIR)/$(REF_PREFIX)__ALL.a
WOLF_LIB := $(WOLF_DIR)/$(WOLF_PREFIX)__ALL.a

BIN := $(OUT_DIR_BASE)/sim_$(CASE_TAG)

CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall
LDLIBS ?= -pthread

.PHONY: run build_ref build_wolf clean

run: $(BIN)
	@if ! command -v $(VERILATOR_COVERAGE) >/dev/null 2>&1; then \
		echo "[$(CASE_TAG)] verilator_coverage not found in PATH"; \
		exit 1; \
	fi
	@echo "[RUN] ./$(BIN)"
	@mkdir -p $(OUT_DIR_BASE)
	@cd $(OUT_DIR_BASE) && VERILATOR_COV_FILE=$(COV_DATA) ./$(notdir $(BIN))
	@$(VERILATOR_COVERAGE) --write-info $(COV_INFO) $(COV_DATA)
	@python3 $(COV_CHECK) $(COV_INFO) $(COV_MIN)

$(WOLF_SV): $(FILELIST)
	@if [ ! -x "$(WOLF_SV_PARSER)" ]; then \
		echo "[$(CASE_TAG)] wolf-sv-parser not found: $(WOLF_SV_PARSER)"; \
		echo "[$(CASE_TAG)] Build it from repo root: cmake -S . -B build && cmake --build build --target wolf-sv-parser"; \
		exit 1; \
	fi
	@mkdir -p $(WOLF_DIR)
	@$(WOLF_SV_PARSER) --emit-sv --emit-json --top $(DUT_TOP) -o $(WOLF_SV) -f $(FILELIST)

$(REF_LIB): $(TB_V) $(FILELIST)
	@if ! command -v $(VERILATOR) >/dev/null 2>&1; then \
		echo "[$(CASE_TAG)] verilator not found in PATH"; \
		exit 1; \
	fi
	@mkdir -p $(REF_DIR)
	@$(VERILATOR) $(REF_FLAGS) --cc $(TB_V) -f $(FILELIST) \
		--top-module $(TB_TOP) --prefix $(REF_PREFIX) -Mdir $(REF_DIR)
	@$(MAKE) -C $(REF_DIR) -f $(REF_PREFIX).mk $(REF_PREFIX)__ALL.a

$(WOLF_LIB): $(TB_V) $(WOLF_SV)
	@if ! command -v $(VERILATOR) >/dev/null 2>&1; then \
		echo "[$(CASE_TAG)] verilator not found in PATH"; \
		exit 1; \
	fi
	@mkdir -p $(WOLF_DIR)
	@$(VERILATOR) $(WOLF_FLAGS) --cc $(TB_V) $(WOLF_SV) \
		--top-module $(TB_TOP) --prefix $(WOLF_PREFIX) -Mdir $(WOLF_DIR)
	@$(MAKE) -C $(WOLF_DIR) -f $(WOLF_PREFIX).mk $(WOLF_PREFIX)__ALL.a

$(BIN): $(REF_LIB) $(WOLF_LIB) $(TB_CPP)
	@if [ -z "$(VERILATOR_ROOT)" ]; then \
		echo "[$(CASE_TAG)] VERILATOR_ROOT not found; set VERILATOR_ROOT=/path/to/verilator"; \
		exit 1; \
	fi
	@mkdir -p $(OUT_DIR_BASE)
	@$(CXX) $(CXXFLAGS) -I$(REF_DIR) -I$(WOLF_DIR) \
		-I$(VERILATOR_ROOT)/include -I$(VERILATOR_ROOT)/include/vltstd \
		$(TB_CPP) $(REF_LIB) $(WOLF_LIB) \
		$(VERILATOR_ROOT)/include/verilated.cpp \
		$(VERILATOR_ROOT)/include/verilated_threads.cpp \
		$(VERILATOR_COV_SRC) \
		-o $(BIN) $(LDLIBS)

clean:
	@rm -rf $(OUT_DIR_BASE)
