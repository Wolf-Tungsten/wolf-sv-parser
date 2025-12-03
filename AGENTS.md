# Repository Guidelines

## Project Structure & Module Organization
- Core sources live in `src/` with headers in `include/`; the CLI entry is `src/main.cpp` linking `grh`, `elaborate`, and `emit`.
- Tests sit under `tests/{grh,elaborate,emit}` with fixtures in `tests/data` and test-only artifacts written to `build/artifacts` (created by CMake).
- Documentation and design notes are in `docs/` (e.g., GRH spec, overview); keep feature-level docs there.
- External dependencies are vendored as git submodules under `external/` (notably `external/slang`); ensure `git submodule update --init --recursive` before building.
- Generated outputs land in `build/bin` (binaries) and `build/hdlbits` (hdlbits sims); avoid committing these.

## Build, Test, and Development Commands
- Configure: `cmake -S . -B build` (requires CMake 3.20+ and a C++20 compiler).
- Build: `cmake --build build -j$(nproc)`; resulting binary is `build/bin/wolf-sv-parser`.
- Tests: `ctest --test-dir build --output-on-failure` after configuring; CTest wraps the per-target executables.
- HDLBits flow: `make run_hdlbits_test DUT=001` (or `make run_all_hdlbits_tests`) builds the parser, emits SV/JSON, and runs Verilator; needs Verilator in PATH.
- Manual run example: `./build/bin/wolf-sv-parser --emit-sv --dump-grh -o out.sv path/to/file.sv`.

## Coding Style & Naming Conventions
- C++20 code with 4-space indentation and braces on the same line as control statements; keep includes ordered and minimal.
- Use the `wolf_sv` namespace with module-specific sub-namespaces; prefer explicit types unless `auto` improves readability.
- Keep public headers in `include/` and implementations in `src/`; lean on `std::filesystem::path` for paths and STL containers for ownership.
- Mirror existing diagnostics/log patterns (e.g., `ElaborateDiagnostics`) and keep comments terse; avoid introducing non-ASCII identifiers.

## Testing Guidelines
- Tests are standalone executables registered via CTest; they fail by returning non-zero. Place new cases alongside their module (e.g., `tests/elaborate/`).
- Name targets descriptively (`emit-json`, `elaborate-smoke`, etc.) and wire fixture paths through `target_compile_definitions` in `CMakeLists.txt`.
- Store fixtures in `tests/data/...`; write artifacts only under `build/artifacts` or `build/hdlbits` to keep the tree clean.
- For HDLBits-style checks, ensure both DUT (`tests/data/hdlbits/dut_*.v`) and TB (`tests/data/hdlbits/tb_*.cpp`) exist and match by ID.

## Commit & Pull Request Guidelines
- Commits follow conventional prefixes (`feat`, `fix`, `test`, `docs`, `chore`, `bump`); keep scopes brief (e.g., `feat: optimize slice emit`).
- Keep changes atomic and include updated fixtures/docs when behavior shifts; run `ctest` and relevant `make run_hdlbits_test` targets before pushing.
- PRs should explain intent, list tests executed, and link related issues; include CLI output snippets or artifact notes when helpful for reviewers.
