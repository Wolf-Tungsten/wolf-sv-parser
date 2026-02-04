---
name: c910-autofix
description: Auto-discover a C910 simulation failure by running make run_c910_test, create a minimal bugcase, reproduce the error with wolf-sv-parser, attempt a fix in wolf-sv-parser, verify the fix, and write a docs/c910 review report. Use for C910 bugcase creation + fix workflows.
---

# C910 Bugcase + Fix Workflow

Use this skill when the user wants an end-to-end flow: detect a C910 simulation error, generate a minimal repro case, fix wolf-sv-parser, and document the fix for human review.

## Inputs

- Optional `CASE_ID`: if the user requests a specific case number; otherwise choose the next available `case_XXX`.
- Optional `USER_LOG`: a user-provided log file path or pasted snippet. Use it to seed repro if provided.

## Workflow

1. Entry paths:
   - If the user provides `USER_LOG`, read it first and extract warnings/errors (focus on error/critical lines, then warnings). Use these messages to identify likely RTL files/modules and the failure signature to reproduce.
   - If no `USER_LOG` is provided or it lacks actionable error context, run `make run_c910_test` from repo root and capture stdout/stderr to a log file (e.g. `build/artifacts/c910_run.log`). Use this log path for all subsequent steps.
2. Read the chosen log (user-provided or freshly generated). Extract the error snippet and locate the referenced RTL file/module.
   - Prefer the RTL file path and module name mentioned in the error or warning.
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
   - TB validates module behavior; treat the original ref RTL as the gold standard for expected behavior.
   - TB is driven by the ref flow; do not modify TB to accommodate wolf output or wolf-only failures.
   - When `VM_COVERAGE` is enabled, write coverage to `VERILATOR_COV_FILE`.
7. Makefile requirements:
   - Targets: `run`, `run_c910_bug_case_ref`, `run_c910_bug_case`, `clean`.
   - `run` uses `--top $(DUT_TOP)` to emit `wolf_emit.sv`.
   - `run_c910_bug_case_ref` runs RTL directly; `run_c910_bug_case` runs `wolf_emit.sv`.
   - Coverage enabled with `COVERAGE=1`; enforce `COV_MIN` (default 90%) for the ref run only.
   - For the wolf run, report coverage but do not fail the run (e.g. use `COV_MIN=0` when invoking it).
   - Outputs under `build/c910_bug_case/case_xxx/{rtl,wolf}` only.
8. Fill `bug_report.md` with the error summary, repro commands, expected vs actual, and minimization notes (include the log path and a snippet).
9. Validate behavior:
   - `make -C tests/data/openc910/bug_cases/case_XXX run_c910_bug_case_ref` must complete without errors and coverage should be close to 90% (adjust TB stimulus to raise coverage; keep `COV_MIN` at 90 unless absolutely necessary).
   - `make -C tests/data/openc910/bug_cases/case_XXX run_c910_bug_case` must reproduce the same class of error message seen in the log (e.g., the same “Value already has a defining operation” failure). If it does not, refine the filelist; do not change TB for wolf-only behavior.
10. Diagnose the root cause in `wolf-sv-parser`:
    - Locate the failing code path (parser, elaborator, or emitter) using the error signature and minimal repro case.
    - Prefer the smallest fix that preserves existing behavior; update or add tests if the fix changes expected output.
11. Apply the fix:
    - Edit `src/` and/or `include/` as needed.
    - Rebuild with `cmake --build build -j$(nproc)` (configure with `cmake -S . -B build` if needed).
12. Verify the fix:
    - Re-run `make -C tests/data/openc910/bug_cases/case_XXX run_c910_bug_case` and confirm the previous error no longer reproduces (report coverage only; do not enforce `COV_MIN`).
    - Run `ctest --test-dir build --output-on-failure` if the fix affects shared logic.
13. Write a fix report for human review under `docs/c910/` (e.g., `docs/c910/case_XXX_fix_report.md`):
    - Problem summary and original failure signature (include log path and a short snippet).
    - Minimal repro details (case path, DUT top, coverage status).
    - Root cause analysis in `wolf-sv-parser`.
    - Fix details (files changed, rationale, risks).
    - Validation results (commands and outcomes).
    - Open questions or follow-ups.
14. Stop after report creation. Do not assume the fix is accepted; the human reviewer decides.

## References

- `docs/c910/openc910调试方案.md`

## Templates

- `assets/Makefile.template`
- `assets/tb_case_xxx.cpp.template`
- `assets/bug_report.md.template`
- `assets/coverage_check.py`
