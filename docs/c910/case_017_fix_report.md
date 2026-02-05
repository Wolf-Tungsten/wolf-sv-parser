# case_017 Fix Report

## Problem summary
- Wolf-emitted SV used an unsized literal (`0`) inside a concatenation when emitting `ct_fcnvt_itof_sh`, triggering Verilator `WIDTHCONCAT` warnings.
- The warning was reproducible in the minimal case with a single leaf module.

## Failure signature
```
%Warning-WIDTHCONCAT: .../wolf_emit.sv:3445:36: Unsized numbers/parameters not allowed in concatenations.
  assign __expr_872 = {__expr_655, 0};
```

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_017`
- DUT top: `ct_fcnvt_itof_sh`
- RTL sources: `../../C910_RTL_FACTORY/gen_rtl/vfalu/rtl/ct_fcnvt_itof_sh.v`
- Ref coverage: 99.92% (1190/1191)
- Wolf coverage: 99.91% (1133/1134)

## Root cause analysis
- `emit.cpp` uses the constant’s `constValue` string verbatim when emitting concatenation and replication operands.
- Some constants carry an unsized literal (e.g. `0`) despite having a known width in the IR, which leads to `{..., 0}` in generated SV and `WIDTHCONCAT` warnings from Verilator.

## Fix details
- Added `sizedLiteralIfUnsized()` in `src/emit.cpp` to format unsized constants using the IR value width when emitting concat/replicate operands.
- Updated concat/replicate emission to use the sized literal helper so `{__expr_655, 0}` becomes `{__expr_655, 32'd0}`.
- Adjusted `tests/data/openc910/bug_cases/case_017/coverage_check.py` to fall back to DA-based line coverage when LH/LF summary lines are absent.
- Files changed: `src/emit.cpp`, `tests/data/openc910/bug_cases/case_017/coverage_check.py`.
- Added repro case: `tests/data/openc910/bug_cases/case_017`.

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_017 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_017 run_c910_bug_case COV_MIN=0`
- Confirmed `WIDTHCONCAT` warnings are gone; existing `UNOPTFLAT` warnings remain (not part of this fix).

## Follow-ups / risks
- Consider normalizing unsized constants earlier in the pipeline so other emit contexts do not require sizing fixes.
- Optional: investigate `UNOPTFLAT` warnings in `ct_fcnvt_itof_sh` if they become problematic.
