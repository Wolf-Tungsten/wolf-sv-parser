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

.PHONY: all build clean

all: build

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) 

$(WOLVRIX_APP): build

clean:
	rm -rf build/
