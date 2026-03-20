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

## Python Bindings (primary usage)

```bash
# Install editable package (from repo root)
python3 -m pip install -e wolvrix

# Basic template (bindings + run_pipeline)
python3 - <<'PY'
import wolvrix

design, _read_diags = wolvrix.read_sv(
    "path/to/file.sv",
    slang_args=["--top", "top"],
    log_level="info",             # dev log output (C++ side, immediate)
    diagnostics="warn",           # diagnostics collection
    print_diagnostics_level="info",
    raise_diagnostics_level="error",
)
pipeline = [
    "xmr-resolve",
    "simplify",
    "memory-init-check",
    "stats",
]
changed, _pass_diags = wolvrix.run_pipeline(
    design,
    pipeline,
    diagnostics="warn",
    log_level="info",
    print_diagnostics_level="info",
    raise_diagnostics_level="error",
)

design.write_json("out.json")
design.write_sv("out.sv")
design.write_sv("out/top_a", top=["top_a"], split_modules=True)
PY
```

`write_sv` now emits only the modules reachable from the selected tops. If `top` is omitted it starts from `design.topGraphs()`. With `split_modules=False` the `output` argument is a single `.sv` file path; with `split_modules=True` it is an output directory and each reachable module is written to `<module_name>.sv`.

### Log vs diagnostics

- **Log**: developer/debug output printed immediately by C++ (not collected). Use `log_level` to control it.
- **Diagnostics**: user-facing messages collected in C++ and returned to Python. Python controls printing via `print_diagnostics_level` and raising via `raise_diagnostics_level`.

Python helpers accept:
- `print_diagnostics_level="info|warn|error|none"`
- `raise_diagnostics_level="error|warn|info|none"` (raise if any diagnostic meets/exceeds this level).

## Tests

```bash
ctest --test-dir wolvrix/build --output-on-failure
```

## License

This project uses the MIT License, same as slang. See `LICENSE`.
