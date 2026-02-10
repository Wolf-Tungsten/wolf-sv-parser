---
name: xs-bugcase
description: Create XiangShan (XS) SV bugcases from wolf-sv-parser parse errors by extracting failing SV sources from build/xs or tests/data/xiangshan/build into tests/data/xs-bugcase/CASE_XXX, and writing tb.v, tb.cpp, and a Makefile to compare ref vs wolf and enforce at least 90% coverage. Use when a user provides a XiangShan SV parse error log and wants a minimal reproducible case.
---

# XS Bugcase Workflow

Use this skill to build a minimal xiangshan SV repro case that compares ref RTL vs wolf-emit and drives coverage above 90%.

## Inputs

- ERROR_LOG: path or pasted snippet from a xiangshan SV parse failure.
- CASE_ID (optional): numeric id for CASE_XXX; otherwise choose next available.
- DUT_TOP (optional): top module name if already known.
- SIGNALS (optional): list of internal signals to export as XMR for comparison.
- FILE_HINTS (optional): list of RTL files or directories to include.

## Workflow

1. Read error log and extract file paths, module names, and line numbers.
   - Prefer paths under `build/xs` or `tests/data/xiangshan/build`.
   - Use `rg -n "module <name>"` or `rg -n "<signal>"` to locate the DUT and dependent modules.
2. Choose case id and create `tests/data/xs-bugcase/CASE_XXX`.
   - Place copied RTL under `tests/data/xs-bugcase/CASE_XXX/rtl` (still under CASE_XXX).
3. Copy RTL sources and include files.
   - Copy only the minimal set needed to reproduce the error.
   - Preserve include relationships; record include dirs in `filelist.f`.
4. Create `filelist.f`.
   - List RTL files relative to CASE_XXX (e.g., `rtl/foo.sv`).
   - Add `+incdir+` for any include directories.
5. Create `tb.v` (wrapper).
   - Instantiate `DUT_TOP`.
   - Expose internal signals via XMR and route them to top-level outputs for `tb.cpp` to compare.
   - Keep wrapper ports stable across ref and wolf builds.
6. Create `tb.cpp` (compare TB).
   - Instantiate `VRef` and `VWolf`.
   - Drive identical stimulus into both.
   - Compare top-level outputs and exported XMR signals each cycle; fail on mismatch.
   - Build enough stimulus to reach >= 90% line coverage on the extracted RTL.
7. Create `Makefile`.
   - Build ref: `tb.v` + `filelist.f` with `--prefix VRef`.
   - Build wolf: run `wolf-sv-parser` to generate `wolf_emit.sv`, then `tb.v` + `wolf_emit.sv` with `--prefix VWolf`.
   - Link both into one binary; run with coverage enabled (ref only) and enforce `COV_MIN=90`.
8. Validate.
   - `make -C tests/data/xs-bugcase/CASE_XXX run`
   - If coverage < 90%, strengthen stimulus in `tb.cpp` and/or expose more internal signals.

## Guardrails

- Do not modify extracted RTL except for strictly necessary include fixes.
- Treat ref RTL as golden behavior; do not tune TB to match wolf output.
- If wolf-sv-parser optimizations remove signals and comparisons break, adjust tb.v/tb.cpp to observe alternative stable signals or XMR paths that still validate equivalence.
- Assume wolf-sv-parser will not optimize or drop top-level port signals.
- Keep all outputs under `build/xs_bugcase/CASE_XXX`.

## Templates

- `assets/Makefile.template`
- `assets/tb.v.template`
- `assets/tb.cpp.template`
- `assets/filelist.f.template`
- `assets/coverage_check.py`
