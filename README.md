# wolvrix

wolvrix converts high-level behavioral RTL (Verilog-2005/SystemVerilog subset) into a structured GRH (Graph RTL Hierarchy) representation, which can then be emitted back as structural Verilog netlists. It supports configurable logic transformations and plugin extensions for simulation, synthesis, formal verification, and custom analysis workflows.

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

The resulting binary will be available at `build/bin/wolvrix-cli`.

## Usage

### Basic Usage

```bash
# Parse a single SystemVerilog file
./build/bin/wolvrix-cli <file.sv>

# Parse multiple files
./build/bin/wolvrix-cli file1.sv file2.sv

# Specify top module
./build/bin/wolvrix-cli --top <module_name> <file.sv>
```

### Command Line Options

#### Input/Output Options

| Option | Description |
|--------|-------------|
| `-o <path>` | Output file path for emitted artifacts. Extension determines format (`.sv`/`.v` for SV, `.json` for JSON) |
| `--emit-out-dir <path>`<br>`--emit-out <path>` | Directory to write emitted GRH/SV files |
| `--emit-sv` | Emit SystemVerilog after conversion and transformation |
| `--emit-json` | Emit GRH JSON representation after conversion |

#### Design Processing Options

| Option | Description |
|--------|-------------|
| `--top <name>` | Specify the top-level module |
| `--define <macro>=<value>` | Define a preprocessor macro |
| `-I <path>` | Add include search path |
| `-L <path>` | Add library search path |
| `--skip-transform` | Skip transform passes and emit raw converted netlist |
| `--single-unit` | Treat all input files as a single compilation unit (enabled by default) |

#### Debug and Logging Options

| Option | Description |
|--------|-------------|
| `--dump-ast` | Dump a summary of the parsed AST |
| `--convert-log` | Enable Convert debug logging |
| `--convert-log-level <level>` | Set Convert log level: `trace`, `debug`, `info`, `warn`, `error`, `off` |
| `--convert-log-tag <tag>` | Limit Convert logging to specific tags (comma-separated) |

#### Performance Options

| Option | Description |
|--------|-------------|
| `--convert-threads <count>` | Number of Convert worker threads (default: 32) |
| `--single-thread` | Force single-threaded Convert execution |
| `--timeout <sec>` | Terminate if runtime exceeds timeout (in seconds) |

For additional options inherited from slang, refer to the [slang documentation](https://sv-lang.com/user-manual.html).

### Usage Examples

#### Parse SV and Output JSON

```bash
./build/bin/wolvrix-cli --emit-json -o output.json --top my_top design.sv
```

#### Parse SV and Output Transformed SV

```bash
./build/bin/wolvrix-cli --emit-sv -o transformed.sv --top my_top design.sv
```

#### Parse with Include Paths and Macros

```bash
./build/bin/wolvrix-cli --emit-sv --emit-json \
    -I ./include -I ./rtl \
    --define SYNTHESIS \
    --define CLOCK_PERIOD=10 \
    -o output.sv design.sv
```

#### Debug with Logging

```bash
./build/bin/wolvrix-cli --emit-sv --convert-log --convert-log-level debug design.sv
```

#### Skip Transform Passes (Raw Netlist)

```bash
./build/bin/wolvrix-cli --emit-sv --skip-transform -o raw.sv design.sv
```

#### Using File Lists

```bash
./build/bin/wolvrix-cli --emit-sv --emit-json -o output.sv -f filelist.f
```

## Project Structure

```
wolvrix/
├── CMakeLists.txt          # CMake build configuration
├── Makefile                # Top-level Makefile with test targets
├── env.sh.template         # Environment configuration template
├── app/                    # Executable applications
│   └── cli/                # CLI app (wolvrix-cli)
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
