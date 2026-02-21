# case_020 fix report - inout vector OE emission

## Problem summary
Verilator reports WIDTHTRUNC warnings when wolf-sv-parser emits inout bindings that use a vector OE as the ternary condition, e.g.:

```
assign b_pad_gpio_porta = b_pad_gpio_porta__oe ? b_pad_gpio_porta__out : {8{1'bz}};
```

This is triggered by C910 GPIO-style per-bit tri-state assigns; the emitted SV collapses per-bit OE into a scalar conditional, which both warns and can change semantics.

- Original log (user snippet): `tests/data/openc910/smart_run/work/wolf_emit/sim_top_wolf.sv` WIDTHTRUNC on `b_pad_gpio_porta__oe`.

## Minimal repro
- Case: `tests/data/openc910/bug_cases/case_020`
- DUT top: `inout_vec_oe`
- Ref RTL coverage: 100% (13/13) using DA fallback
- Wolf RTL coverage: 100% (20/20) using DA fallback

## Root cause
The emitter always generated a scalar conditional for inout bindings regardless of OE width. Inout OE values are created at port width, including per-bit OE assembled from slice writes, so vector OEs were treated as scalar conditions, producing WIDTHTRUNC and incorrect per-bit tri-state behavior.

## Fix
1. `src/convert.cpp`: when an inout assignment uses a 1-bit OE and the port is wider, replicate the OE to the full port width instead of zero-extending. This keeps per-bit semantics consistent once emission is per-bit.
2. `src/emit.cpp`: emit per-bit assign statements when OE width equals port width; keep scalar ternary when OE width is 1; otherwise fall back to an explicit `!= 0` test to avoid width truncation warnings. Per-bit assigns avoid literal bit-selects by using shift/mask when OE/OUT are constants (prevents Verilator syntax errors like `(8'b0)[5]`).

## Validation
- `cmake --build build -j$(nproc)`
- `make -C tests/data/openc910/bug_cases/case_020 run_c910_bug_case_ref`
- `make -C tests/data/openc910/bug_cases/case_020 run_c910_bug_case`
- `ctest --test-dir build --output-on-failure`

## Open questions / follow-ups
- If any designs intentionally use a multi-bit OE as a scalar condition (reduction-style), confirm that per-bit emission is acceptable or consider an explicit reduction attribute to preserve intent.
