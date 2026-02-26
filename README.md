# wolvrix

wolvrix converts high-level behavioral RTL (Verilog-2005/SystemVerilog subset) into a structured GRH (Graph RTL Hierarchy) representation, which can then be emitted back as structural Verilog designs. It supports configurable logic transformations and plugin extensions for simulation, synthesis, formal verification, and custom analysis workflows.

## Project Overview

wolvrix is a SystemVerilog parser built on [slang](https://github.com/MikePopoloski/slang), designed for analyzing and processing SystemVerilog RTL designs.

## Quick Start (Submodules + CMake)

```bash
# Initialize submodules
git submodule update --init --recursive

# Build with CMake (from repo root)
cmake -S wolvrix -B wolvrix/build
cmake --build wolvrix/build -j$(nproc)
```

The resulting binary will be available at `wolvrix/build/bin/wolvrix`.

## Python Bindings

```bash
# Install editable package (from repo root)
python3 -m pip install -e wolvrix

# Example script usage
python3 - <<'PY'
import wolvrix

design = wolvrix.read_sv("path/to/file.sv", slang_args=["--top", "top"], log_level="warn")
design.write_json("out.json")
design.write_sv("out.sv")
PY
```

## Tests

```bash
ctest --test-dir wolvrix/build --output-on-failure
```

## License

This project uses the MIT License, same as slang. See `LICENSE`.
