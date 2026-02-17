# CASE_008: Register Initialization Test Makefile
ROOT := $(abspath ../../../..)
CASE_ID := 008
CASE_TAG := CASE_$(CASE_ID)
DUT_TOP := reg_init_test
TB_TOP := xs_bugcase_tb
CASE_DIR := $(CURDIR)
FILELIST := $(CASE_DIR)/filelist.f
TB_V := $(CASE_DIR)/tb.v
TB_CPP := $(CASE_DIR)/tb.cpp
OUT_DIR_BASE := $(ROOT)/build/xs_bugcase/$(CASE_TAG)
REF_DIR := $(OUT_DIR_BASE)/ref
WOLF_DIR := $(OUT_DIR_BASE)/wolf
WOLF_SV := $(WOLF_DIR)/wolf_emit.sv
BIN := $(OUT_DIR_BASE)/sim_$(CASE_TAG)

BASE_FLAGS := --no-timing -Wall -Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL
REF_FLAGS := $(BASE_FLAGS)
WOLF_FLAGS := $(BASE_FLAGS)
REF_PREFIX := VRef
WOLF_PREFIX := VWolf
REF_LIB := $(REF_DIR)/$(REF_PREFIX)__ALL.a
WOLF_LIB := $(WOLF_DIR)/$(WOLF_PREFIX)__ALL.a
VERILATOR := verilator
VERILATOR_ROOT := $(shell $(VERILATOR) --getenv VERILATOR_ROOT)
WOLF_SV_PARSER := $(ROOT)/build/bin/wolf-sv-parser

CXXFLAGS := -std=c++17 -O2 -Wall -DVM_TRACE=0

.PHONY: run build_ref build_wolf clean

run: $(BIN)
	@echo "[RUN] ./$(BIN)"
	@mkdir -p $(OUT_DIR_BASE)
	@cd $(OUT_DIR_BASE) && ./$(notdir $(BIN))

$(WOLF_SV): $(FILELIST)
	@if [ ! -x "$(WOLF_SV_PARSER)" ]; then \
		echo "[$(CASE_TAG)] wolf-sv-parser not found: $(WOLF_SV_PARSER)"; \
		exit 1; \
	fi
	@mkdir -p $(WOLF_DIR)
	@$(WOLF_SV_PARSER) --emit-sv --emit-json --top $(DUT_TOP) -o $(WOLF_SV) -f $(FILELIST)

$(REF_LIB): $(FILELIST)
	@if ! command -v $(VERILATOR) >/dev/null 2>&1; then \
		echo "[$(CASE_TAG)] verilator not found in PATH"; \
		exit 1; \
	fi
	@mkdir -p $(REF_DIR)
	@$(VERILATOR) $(REF_FLAGS) --cc -f $(FILELIST) \
		--top-module $(DUT_TOP) --prefix $(REF_PREFIX) -Mdir $(REF_DIR)
	@$(MAKE) -C $(REF_DIR) -f $(REF_PREFIX).mk $(REF_PREFIX)__ALL.a

$(WOLF_LIB): $(WOLF_SV)
	@if ! command -v $(VERILATOR) >/dev/null 2>&1; then \
		echo "[$(CASE_TAG)] verilator not found in PATH"; \
		exit 1; \
	fi
	@mkdir -p $(WOLF_DIR)
	@$(VERILATOR) $(WOLF_FLAGS) --cc $(WOLF_SV) \
		--top-module $(DUT_TOP) --prefix $(WOLF_PREFIX) -Mdir $(WOLF_DIR)
	@$(MAKE) -C $(WOLF_DIR) -f $(WOLF_PREFIX).mk $(WOLF_PREFIX)__ALL.a

$(BIN): $(REF_LIB) $(WOLF_LIB) $(TB_CPP)
	@if [ -z "$(VERILATOR_ROOT)" ]; then \
		echo "[$(CASE_TAG)] VERILATOR_ROOT not found"; \
		exit 1; \
	fi
	@mkdir -p $(OUT_DIR_BASE)
	@$(CXX) $(CXXFLAGS) -I$(REF_DIR) -I$(WOLF_DIR) \
		-I$(VERILATOR_ROOT)/include -I$(VERILATOR_ROOT)/include/vltstd \
		$(TB_CPP) $(REF_LIB) $(WOLF_LIB) \
		$(VERILATOR_ROOT)/include/verilated.cpp \
		$(VERILATOR_ROOT)/include/verilated_threads.cpp \
		-o $(BIN)

clean:
	@rm -rf $(OUT_DIR_BASE)
