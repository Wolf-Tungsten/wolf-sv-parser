---
name: c910-bugcase
description: Create a new OpenC910 bugcase from a make run_c910_test output log (or run it when not provided), and validate ref sim + coverage and wolf repro.
metadata:
  short-description: Create C910 bugcase from error file
---

# C910 Bugcase Creator

Use this skill when the user asks to create a C910 bugcase based on a `make run_c910_test` output log. The input is a repo-relative path to that log; if the user does not provide it, the skill must run the command and capture the log.

## Inputs

- `RUN_LOG`: repo-relative path to the captured `make run_c910_test` output log. If absent, run `make run_c910_test` from repo root and save the output to a log file (e.g. `build/artifacts/c910_run.log`), then use that path as `RUN_LOG`.
- Optional `CASE_ID`: if the user requests a specific case number; otherwise choose the next available `case_XXX`.

## Workflow

1. Ensure `RUN_LOG` exists.
   - If the user did not provide a log, run `make run_c910_test` from repo root, capture stdout/stderr to a log file, and set `RUN_LOG` to that file.
2. Read `RUN_LOG`. Extract the error snippet and locate the referenced RTL file/module.
   - Prefer the RTL file path and module name mentioned in the error.
3. Determine `DUT_TOP` and minimal RTL sources.
   - Use `rg -n "module <DUT_TOP>"` in `tests/data/openc910/C910_RTL_FACTORY` to find the module file.
   - Keep `filelist.f` minimal; prefer existing RTL paths. Add `stub_modules.v` only when needed.
4. Create a new case directory `tests/data/openc910/bug_cases/case_XXX`.
   - Choose the next available index unless `CASE_ID` is provided.
5. Create files (no `tb_case_xxx.v`; test the module directly):
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
6. TB requirements:
   - Include `V<DUT_TOP>.h`, implement clock/reset, deterministic stimulus, and at least one correctness check.
   - Ensure the stimulus exercises enough logic so that coverage is close to 90% when running `run_c910_bug_case_ref`.
   - When `VM_COVERAGE` is enabled, write coverage to `VERILATOR_COV_FILE`.
7. Makefile requirements:
   - Targets: `run`, `run_c910_bug_case_ref`, `run_c910_bug_case`, `clean`.
   - `run` uses `--top $(DUT_TOP)` to emit `wolf_emit.sv`.
   - `run_c910_bug_case_ref` runs RTL directly; `run_c910_bug_case` runs `wolf_emit.sv`.
   - Coverage enabled with `COVERAGE=1` and checked against `COV_MIN` (default 90%).
   - Outputs under `build/c910_bug_case/case_xxx/{rtl,wolf}` only.
8. Fill `bug_report.md` with the error summary, repro commands, expected vs actual, and minimization notes (include the `RUN_LOG` path and a snippet).
9. Validate behavior:
   - `make -C tests/data/openc910/bug_cases/case_XXX run_c910_bug_case_ref` must complete without errors and coverage should be close to 90% (adjust TB stimulus to raise coverage; keep `COV_MIN` at 90 unless absolutely necessary).
   - `make -C tests/data/openc910/bug_cases/case_XXX run_c910_bug_case` must reproduce the same class of error message seen in `RUN_LOG` (e.g., the same “Value already has a defining operation” failure). If it does not, refine the filelist or TB to match the failing path.

## References

- `docs/c910/openc910调试方案.md`

## Templates

- `assets/Makefile.template`
- `assets/tb_case_xxx.cpp.template`
- `assets/bug_report.md.template`
- `assets/coverage_check.py`
