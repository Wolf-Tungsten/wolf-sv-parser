# wolvrix

wolvrix converts high-level behavioral RTL (Verilog-2005/SystemVerilog subset) into a structured GRH (Graph RTL Hierarchy) representation, which can then be emitted back as structural Verilog designs. It supports configurable logic transformations and plugin extensions for simulation, synthesis, formal verification, and custom analysis workflows.

## Project Overview

wolvrix is a SystemVerilog parser built on [slang](https://github.com/MikePopoloski/slang), designed for analyzing and processing SystemVerilog RTL designs.

## Building the Project

### Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 2019+)
- Git

### OpenC910 Test Environment Configuration (Optional)

If you need to run OpenC910-related tests (e.g., `make run_c910_test`), configure the environment:

```bash
# 1. Copy the environment template
cp env.sh.template env.sh

# 2. Edit env.sh and configure the following variables:
#    - TOOL_EXTENSION: Path to RISC-V toolchain (required for OpenC910 tests)
#    - VERILATOR:      Path to verilator executable (optional)
```

**Note**: 
- `env.sh` is added to `.gitignore` and will not be tracked by git
- The top-level Makefile automatically loads `env.sh` if it exists, no manual `source` needed
- You can also use `make TOOL_EXTENSION=/path VERILATOR=/path` for one-time overrides

RISC-V Toolchain Download: [Xuantie Toolchain Download Page](https://www.xrvm.com/community/download?id=4058840482644627456)

### Build Steps

```bash
# Clone the repository (including submodules)
git clone --recursive https://github.com/Wolf-Tungsten/wolf-sv-parser.git
cd wolf-sv-parser

# If already cloned but without submodules, run:
git submodule update --init --recursive

# Build the project
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
```

The resulting binary will be available at `build/bin/wolvrix`.

## Usage

### Wolvrix App (Tcl/REPL)

Use `-f` for scripts or `-c` for one-liners.

```tcl
# flow.tcl
read_sv design.sv --top my_top
foreach pass {xmr-resolve const-fold redundant-elim memory-init-check dead-code-elim stats} {
  transform $pass
}
write_json -o output.json
write_sv -o output.sv
```

```bash
./build/bin/wolvrix -f flow.tcl
```

```bash
./build/bin/wolvrix -c "read_sv design.sv --top my_top; write_sv -o output.sv"
```

### Tcl Command Notes

Common Tcl commands include:
- `read_sv <file> ?<slang-opts>...?`
- `transform <passname> ?passargs...?`
- `write_json -o <file>`
- `write_sv -o <file>`

## Project Structure

```
wolvrix/
├── CMakeLists.txt          # CMake build configuration
├── Makefile                # Top-level Makefile with test targets
├── env.sh.template         # Environment configuration template
├── app/                    # Executable applications
│   └── wolvrix/            # Tcl/REPL app
├── lib/                    # Core library (SDK)
│   ├── include/            # Public headers
│   ├── src/                # Core implementations
│   └── transform/          # Built-in transform passes
├── tests/                  # Test suites
│   ├── data/               # Test data and fixtures
│   └── ...                 # Unit tests
├── external/               # External dependencies
│   └── slang/              # slang submodule
└── build/                  # Build directory (generated)
```

## License

This project uses the MIT License, same as slang.
