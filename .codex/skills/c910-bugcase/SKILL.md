---
name: c910-bugcase
description: Create a new OpenC910 bugcase from a user-provided error file path.
metadata:
  short-description: Create C910 bugcase from error file
---

# C910 Bugcase Creator

Use this skill when the user asks to create a C910 bugcase based on an error file path. The input is a repo-relative path to an error file (RTL or log).

## Inputs

- `ERROR_FILE`: repo-relative path provided by the user.
- Optional `CASE_ID`: if the user requests a specific case number; otherwise choose the next available `case_XXX`.

## Workflow

1. Validate `ERROR_FILE` exists and read it. Extract the error snippet and locate a candidate RTL file/module.
   - If `ERROR_FILE` is RTL, use it as the seed.
   - If it is a log, find the referenced RTL file and module name.
2. Determine `DUT_TOP` and minimal RTL sources.
   - Use `rg -n "module <DUT_TOP>"` in `tests/data/openc910/C910_RTL_FACTORY` to find the module file.
   - Keep `filelist.f` minimal; prefer existing RTL paths. Add `stub_modules.v` only when needed.
3. Create a new case directory `tests/data/openc910/bug_cases/case_XXX`.
   - Choose the next available index unless `CASE_ID` is provided.
4. Create files (no `tb_case_xxx.v`; test the module directly):
   - `filelist.f` (RTL only)
   - `tb_case_xxx.cpp` (drives DUT directly)
   - `Makefile`
   - `bug_report.md`
   - `coverage_check.py`
   - optional `stub_modules.v`
   Use templates from `assets/` and replace placeholders:
   - `CASE_ID` -> `001` (or chosen index)
   - `DUT_TOP` -> module name
   - `__DUT_TOP__` -> module name (for `V<top>` symbols)
5. TB requirements:
   - Include `V<DUT_TOP>.h`, implement clock/reset, deterministic stimulus, and at least one correctness check.
   - When `VM_COVERAGE` is enabled, write coverage to `VERILATOR_COV_FILE`.
6. Makefile requirements:
   - Targets: `run`, `run_c910_bug_case_ref`, `run_c910_bug_case`, `clean`.
   - `run` uses `--top $(DUT_TOP)` to emit `wolf_emit.sv`.
   - `run_c910_bug_case_ref` runs RTL directly; `run_c910_bug_case` runs `wolf_emit.sv`.
   - Coverage enabled with `COVERAGE=1` and checked against `COV_MIN` (default 90%).
   - Outputs under `build/c910_bug_case/case_xxx/{rtl,wolf}` only.
7. Fill `bug_report.md` with the error summary, repro commands, expected vs actual, and minimization notes.

## References

- `docs/c910/openc910调试方案.md`

## Templates

- `assets/Makefile.template`
- `assets/tb_case_xxx.cpp.template`
- `assets/bug_report.md.template`
- `assets/coverage_check.py`
