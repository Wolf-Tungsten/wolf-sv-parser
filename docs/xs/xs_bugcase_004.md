# XS Bugcase 004: Sized-Cast Width Collapse

## Summary
This bugcase isolates a width/resize lowering error where a sized cast like `9'(...)` is effectively reduced to LSB-only, causing a compare against `9'd224` to fail.

## Repro
```
make -C tests/data/xs-bugcase/CASE_004 run
```
Coverage is disabled by default in this bugcase. To enable it:
```
make -C tests/data/xs-bugcase/CASE_004 run COVERAGE=1
```

## Expected Behavior
- Ref model: `sum = 224`, `bad = 0`.
- Wolf emit (current): `sum` collapses to LSB-derived value, `bad = 1`, triggering a mismatch.

## RTL Under Test
`tests/data/xs-bugcase/CASE_004/rtl/ExprCastBug.sv`

Inputs are chosen to mirror the MEFreeList formula:
- `flag_a/flag_b` and `val_a/val_b` approximate the `distanceBetween` path.
- `b0/b1/b2` approximate the nested popcount sum.

## Notes
The expression mirrors the MEFreeList pattern:
```
9'({1'b0, a} + {3'b0, 6'({1'b0, b})})
```
The wolf emit currently lowers this into a chain that selects `[0]` from the sized-cast result.
