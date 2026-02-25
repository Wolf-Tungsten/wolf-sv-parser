# wolvrix

wolvrix converts high-level behavioral RTL (Verilog-2005/SystemVerilog subset) into a structured GRH (Graph RTL Hierarchy) representation, which can then be emitted back as structural Verilog designs. It supports configurable logic transformations and plugin extensions for simulation, synthesis, formal verification, and custom analysis workflows.

## Project Overview

wolvrix is a SystemVerilog parser built on [slang](https://github.com/MikePopoloski/slang), designed for analyzing and processing SystemVerilog RTL designs.

## Quick Start (Submodules + CMake)

```bash
# Initialize submodules
git submodule update --init --recursive

# Build with CMake
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
```

The resulting binary will be available at `build/bin/wolvrix`.

## License

This project uses the MIT License, same as slang.
