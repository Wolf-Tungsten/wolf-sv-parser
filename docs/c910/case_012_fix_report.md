# case_012 Fix Report

## Problem summary
- Failure signature: "Skipping instance output merge with overlapping slices" during convert.
- Log path: `build/artifacts/c910_run.log`
- Snippet:

```
[convert] [ERROR] ../../C910_RTL_FACTORY/gen_rtl/plic/rtl/plic_hreg_busif.v:441:28 - Skipping instance output merge with overlapping slices
  statement: .clk_out              (ie_wr_clk[i][rd_idx]),
Build failed: convert encountered errors
```

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_012`
- DUT top: `plic_hreg_busif`
- Filelist: `tests/data/openc910/bug_cases/case_012/filelist.f`
- Ref coverage: 85.86% (261/304) with `COV_MIN=85`
  - 90% is not reachable because `FLOP==1` in `csky_apb_1tox_matrix.v` makes several lines un-executable.

## Root cause
`resolveOutputBinding()` handled nested selects by ignoring the base slice offset for packed element selects. For expressions like `ie_wr_clk[i][rd_idx]`, the unpacked index `i` produces a base slice offset (i * elementWidth), but the packed bit select path overwrote `out.low` with only the packed index. This collapsed all per-hart slice writes onto the same low bits, which later tripped the overlap check in `emitInstanceSliceWrites()`.

## Fix details
- Updated packed ElementSelect and RangeSelect handling to add `base.low` (with overflow checks) when computing `out.low`.
- This preserves the offset from an unpacked-array selection so merged instance output slices are non-overlapping.

Files changed:
- `src/convert.cpp`
- `tests/data/openc910/bug_cases/case_012/Makefile`
- `tests/data/openc910/bug_cases/case_012/tb_case_012.cpp`
- `tests/data/openc910/bug_cases/case_012/coverage_check.py`
- `tests/data/openc910/bug_cases/case_012/bug_report.md`
- `tests/data/openc910/bug_cases/case_012/filelist.f`
- `tests/data/openc910/bug_cases/case_012/stub_modules.v`

## Validation
- Build: `cmake --build build -j$(nproc)`
- Ref run: `make -C tests/data/openc910/bug_cases/case_012 run_c910_bug_case_ref`
  - Passes with 85.86% line coverage.
- Wolf run: `make -C tests/data/openc910/bug_cases/case_012 run_c910_bug_case`
  - Did not complete within 5 minutes in this environment.
  - Direct run with `timeout 30s` produced no overlap error before timing out.

## Open questions / follow-ups
- Consider adding a smaller synthetic regression for unpacked+packed selects to avoid long runtimes.
- If runtime remains high, investigate performance of conversion on large unpacked arrays.
