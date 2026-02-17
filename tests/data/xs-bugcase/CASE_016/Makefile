ROOT := $(abspath ../../../..)

CASE_ID := 016
CASE_TAG := CASE_$(CASE_ID)

DUT_TOP := Ftq
TB_TOP := xs_bugcase_tb

CASE_DIR := $(CURDIR)
FILELIST := $(CASE_DIR)/filelist.f
TB_V := $(CASE_DIR)/tb.v
TB_CPP := $(CASE_DIR)/tb.cpp

OUT_DIR_BASE := $(ROOT)/build/xs_bugcase/$(CASE_TAG)
REF_DIR := $(OUT_DIR_BASE)/ref
WOLF_DIR := $(OUT_DIR_BASE)/wolf

export CCACHE_DIR := $(OUT_DIR_BASE)/ccache

WOLF_SV_PARSER ?= $(ROOT)/build/bin/wolf-sv-parser
WOLF_SV := $(WOLF_DIR)/wolf_emit.sv

VERILATOR ?= $(or $(shell echo $$VERILATOR),verilator)
VERILATOR_COVERAGE ?= verilator_coverage
VERILATOR_ROOT ?= $(shell $(VERILATOR) -V 2>/dev/null | awk -F'= *' '/^VERILATOR_ROOT/ {print $$2}')
VERILATOR_COV_SRC = $(if $(filter 1,$(COVERAGE)),$(VERILATOR_ROOT)/include/verilated_cov.cpp,)

COVERAGE ?= 0
COV_MIN ?= 0
COV_DATA := $(OUT_DIR_BASE)/coverage.dat
COV_INFO := $(OUT_DIR_BASE)/coverage.info
COV_CHECK := $(CASE_DIR)/coverage_check.py

BASE_FLAGS := --no-timing -Wall -Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL \
	-Wno-UNDRIVEN -Wno-SYNCASYNCNET -Wno-PINCONNECTEMPTY -Wno-ASSIGNDLY -Wno-MULTIDRIVEN \
	-Wno-CASEINCOMPLETE -Wno-EOFNEWLINE
REF_FLAGS := $(BASE_FLAGS) $(if $(filter 1,$(COVERAGE)),--coverage,)
WOLF_FLAGS := $(BASE_FLAGS)

REF_PREFIX := VRef
WOLF_PREFIX := VWolf
REF_LIB := $(REF_DIR)/$(REF_PREFIX)__ALL.a
WOLF_LIB := $(WOLF_DIR)/$(WOLF_PREFIX)__ALL.a

BIN := $(OUT_DIR_BASE)/sim_$(CASE_TAG)

CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall
SAN ?= 0
SAN_FLAGS :=
ifeq ($(SAN),1)
SAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer -g
endif
CXXFLAGS += $(SAN_FLAGS)
LDLIBS ?= -pthread
LDLIBS += $(SAN_FLAGS)

# Colors for output
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[0;33m
NC := \033[0m # No Color

.PHONY: all run test clean build_ref build_wolf help

# Default target: build and run everything
all: run

help:
	@echo "CASE_016: inner_ftq io_fromBpu_prediction_ready divergence"
	@echo ""
	@echo "Targets:"
	@echo "  make             - Build everything and run test (default)"
	@echo "  make run         - Build and run test"
	@echo "  make quick       - Run test without rebuilding (if binary exists)"
	@echo "  make test        - Same as 'make run'"
	@echo "  make build_ref   - Build reference (original RTL)"
	@echo "  make build_wolf  - Build wolf (wolf-sv-parser processed)"
	@echo "  make clean       - Remove all build artifacts"
	@echo "  make help        - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  make             - Full build and test (~3 minutes)"
	@echo "  make quick       - Quick test only (~1 second)"
	@echo ""
	@echo "Environment:"
	@echo "  WOLF_SV_PARSER=$(WOLF_SV_PARSER)"
	@echo "  VERILATOR=$(VERILATOR)"

# One-step build and run
run: $(BIN)
	@echo "$(GREEN)[CASE_016] Running test...$(NC)"
	@cd $(OUT_DIR_BASE) && ./$(notdir $(BIN)) 2>&1 | tee test.log || (echo "$(RED)[CASE_016] Test FAILED$(NC)" && exit 1)
	@echo "$(GREEN)[CASE_016] Test completed$(NC)"

# Quick run without rebuilding
quick:
	@if [ -f $(BIN) ]; then \
		echo "$(GREEN)[CASE_016] Running test (quick mode)...$(NC)"; \
		cd $(OUT_DIR_BASE) && ./$(notdir $(BIN)) 2>&1 && echo "$(GREEN)[CASE_016] Test passed (no mismatch)$(NC)" || echo "$(YELLOW)[CASE_016] Test completed (mismatch detected as expected)$(NC)"; \
	else \
		echo "$(YELLOW)[CASE_016] Binary not found, building first...$(NC)"; \
		$(MAKE) run; \
	fi

test: run

# Build reference simulation (original RTL)
build_ref: $(REF_LIB)
	@echo "$(GREEN)[CASE_016] Reference build complete$(NC)"

# Build wolf simulation (wolf-sv-parser processed)
build_wolf: $(WOLF_LIB)
	@echo "$(GREEN)[CASE_016] Wolf build complete$(NC)"

# Verilate and compile reference
$(REF_LIB): $(TB_V) $(FILELIST)
	@echo "$(YELLOW)[CASE_016] Building reference...$(NC)"
	@if ! command -v $(VERILATOR) >/dev/null 2>&1; then \
		echo "$(RED)[CASE_016] verilator not found in PATH$(NC)"; \
		exit 1; \
	fi
	@mkdir -p $(REF_DIR)
	@ln -sf $(CASE_DIR)/rtl $(REF_DIR)/rtl
	@echo "  Verilating..."
	@$(VERILATOR) $(REF_FLAGS) --cc $(TB_V) -f $(FILELIST) \
		--top-module $(TB_TOP) --prefix $(REF_PREFIX) -Mdir $(REF_DIR) \
		2>&1 | grep -E "^%Error|^%Warning|^- Verilation" || true
	@echo "  Compiling..."
	@$(MAKE) -C $(REF_DIR) -f $(REF_PREFIX).mk $(REF_PREFIX)__ALL.a CXXFLAGS="$(CXXFLAGS)" >/dev/null 2>&1

# Run wolf-sv-parser and verilate wolf
$(WOLF_SV): $(FILELIST)
	@echo "$(YELLOW)[CASE_016] Running wolf-sv-parser...$(NC)"
	@if [ ! -x "$(WOLF_SV_PARSER)" ]; then \
		echo "$(RED)[CASE_016] wolf-sv-parser not found: $(WOLF_SV_PARSER)$(NC)"; \
		echo "$(RED)[CASE_016] Build it from repo root: cmake -S . -B build && cmake --build build --target wolf-sv-parser$(NC)"; \
		exit 1; \
	fi
	@mkdir -p $(WOLF_DIR)
	@$(WOLF_SV_PARSER) --emit-sv --emit-json --top $(DUT_TOP) -o $(WOLF_SV) -f $(FILELIST) \
		2>&1 | grep -E "^\[|^%Error" || true
	@echo "$(GREEN)[CASE_016] wolf-sv-parser complete$(NC)"

# Verilate and compile wolf
$(WOLF_LIB): $(TB_V) $(WOLF_SV)
	@echo "$(YELLOW)[CASE_016] Building wolf...$(NC)"
	@if ! command -v $(VERILATOR) >/dev/null 2>&1; then \
		echo "$(RED)[CASE_016] verilator not found in PATH$(NC)"; \
		exit 1; \
	fi
	@mkdir -p $(WOLF_DIR)
	@echo "  Verilating..."
	@$(VERILATOR) $(WOLF_FLAGS) --cc $(TB_V) $(WOLF_SV) \
		--top-module $(TB_TOP) --prefix $(WOLF_PREFIX) -Mdir $(WOLF_DIR) \
		2>&1 | grep -E "^%Error|^%Warning|^- Verilation" || true
	@echo "  Compiling..."
	@$(MAKE) -C $(WOLF_DIR) -f $(WOLF_PREFIX).mk $(WOLF_PREFIX)__ALL.a CXXFLAGS="$(CXXFLAGS)" >/dev/null 2>&1

# Link final binary
$(BIN): $(REF_LIB) $(WOLF_LIB) $(TB_CPP)
	@echo "$(YELLOW)[CASE_016] Linking...$(NC)"
	@if [ -z "$(VERILATOR_ROOT)" ]; then \
		echo "$(RED)[CASE_016] VERILATOR_ROOT not found; set VERILATOR_ROOT=/path/to/verilator$(NC)"; \
		exit 1; \
	fi
	@mkdir -p $(OUT_DIR_BASE)
	@$(CXX) $(CXXFLAGS) -I$(REF_DIR) -I$(WOLF_DIR) \
		-I$(VERILATOR_ROOT)/include -I$(VERILATOR_ROOT)/include/vltstd \
		$(TB_CPP) $(REF_LIB) $(WOLF_LIB) \
		$(VERILATOR_ROOT)/include/verilated.cpp \
		$(VERILATOR_ROOT)/include/verilated_threads.cpp \
		$(VERILATOR_ROOT)/include/verilated_cov.cpp \
		-o $(BIN) $(LDLIBS) 2>&1 | (grep -E "^/|error:|Error:" || true)
	@test -f $(BIN) && echo "$(GREEN)[CASE_016] Binary ready: $(BIN)$(NC)" || (echo "$(RED)[CASE_016] Link failed$(NC)" && exit 1)

clean:
	@echo "$(YELLOW)[CASE_016] Cleaning...$(NC)"
	@rm -rf $(OUT_DIR_BASE)
	@echo "$(GREEN)[CASE_016] Clean complete$(NC)"

# Debug targets
print_vars:
	@echo "ROOT=$(ROOT)"
	@echo "CASE_DIR=$(CASE_DIR)"
	@echo "OUT_DIR_BASE=$(OUT_DIR_BASE)"
	@echo "WOLF_SV=$(WOLF_SV)"
	@echo "BIN=$(BIN)"
