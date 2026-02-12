# XS Bugcase 003: Signed compare turns unsigned in wolf emit

## Summary

`wolf_emit.sv` converts a signed comparison in `VSegmentUnit` into an unsigned
comparison, triggering Verilator `%Warning-UNSIGNED`:

```
assign __expr_803 = _emul_T_3 < 3'b0; // unsigned compare in wolf_emit
```

The reference RTL uses a signed compare:

```
$signed(_emul_T_3) < 3'sh0
```

## Reproduction

```
make -C tests/data/xs-bugcase/CASE_003 run
```

Verilator warning (from wolf emit):

```
%Warning-UNSIGNED: .../wolf_emit.sv:4304:33: Comparison is constant due to unsigned arithmetic
  assign __expr_803 = _emul_T_3 < 3'b0;
```

## Evidence (ref vs wolf)

**Ref RTL (tests/data/xs-bugcase/CASE_003/rtl/VSegmentUnit.sv:1061):**

```
: $signed(_emul_T_3) < 3'sh0 ? 4'h1 : _splitPtrOffset_T_8[3:0]))
```

**Wolf emit (build/xs_bugcase/CASE_003/wolf/wolf_emit.sv:4304):**

```
assign __expr_803 = _emul_T_3 < 3'b0;
```

## Root Cause

From the generated GRH (`build/xs_bugcase/CASE_003/wolf/grh.json`), the compare
operands are:

- `__expr_802` **signed** (`sgn: true`) — this is the `$signed(_emul_T_3)` value
- `__const_40` **unsigned** (`sgn: false`) — constant literal `3'b0`

Because the emitter only applies `$signed(...)` when **both** operands are signed
(see `src/emit.cpp`), the mixed signedness forces an unsigned compare.

The unsigned constant originates from the constant lowering path:

- Integer literal nodes are created without `isSigned` set (defaults to `false`).
- `emitConstant()` currently creates GRH values with `isSigned = false` **unconditionally**,
  so even signed literals (like `3'sh0`) lose their signedness.
- Const-fold deduplication then reuses this unsigned `3'b0` across the module.

## Why this becomes a warning

In SV, if either operand is unsigned, the comparison is unsigned. With an
unsigned constant zero, `_emul_T_3 < 3'b0` is always false because `_emul_T_3`
was produced from a 3-bit unsigned expression. Verilator correctly reports
this as a constant comparison under unsigned arithmetic.

## Fix Options

### Option A (Preferred): Preserve literal signedness end-to-end

1) **Set `ExprNode.isSigned` for integer literals**
   - In `lowerExpression()` when handling `IntegerLiteral` and
     `UnbasedUnsizedIntegerLiteral`, set:

   ```cpp
   node.isSigned = literal->getValue().isSigned();
   // or: node.isSigned = expr.type ? expr.type->isSigned() : false;
   ```

2) **Honor `node.isSigned` when emitting constants**
   - In `emitConstant()` (convert → GRH), use:

   ```cpp
   graph_.createValue(symbol, width, node.isSigned, node.valueType);
   ```

This keeps `3'sh0` as a signed constant (`sgn: true`) in GRH, which in turn
makes the emitter generate a signed comparison.

### Option B: Force signed compare when one operand is a `$signed` result

Modify the emitter logic for relational ops to apply `$signed(...)` when the
left operand is signed and the right operand is an unsized/zero constant. This
is more ad-hoc and risks deviating from SV semantics elsewhere.

## Expected Outcome After Fix

The emitted compare should become one of the following equivalent forms:

```
assign __expr_803 = $signed(__expr_802) < $signed(3'b0);
// or
assign __expr_803 = $signed(_emul_T_3) < 3'sh0;
```

which removes the unsigned-constant warning and matches the reference RTL
intent.

## Notes

- The GRH already marks the `$signed(_emul_T_3)` value as signed. The missing
  piece is the signedness of the constant literal.
- This bugcase also produces other Verilator warnings (WIDTHEXPAND, CMPCONST),
  but the focus here is the `UNSIGNED` warning tied to the signed/unsigned
  mismatch.

## Follow-up: Packed array index scaling bug (stateNext mismatch)

After fixing signed literals, the next failure in CASE_003 showed a behavioral
divergence in `stateNext`. The ref RTL has:

```
wire [15:0][3:0] _GEN_14 = { ... };
wire [3:0] _GEN_15 = _GEN_14[state];
wire [3:0] stateNext = (|state) ? _GEN_15 : _stateNext_T_1;
```

In GRH, `_GEN_14` is flattened into a 64-bit packed vector via `kConcat`, but
the element select was lowered as `kSliceDynamic` with **bit index = state**.
That produced `wolf_emit.sv` like:

```
assign _GEN_15 = _GEN_14[state +: 4];
```

This is incorrect for packed arrays; the bit index must scale by the element
width (4). The fix is to scale packed-array element indices during lowering:

- In `src/convert.cpp`, `adjustPackedIndex()` now multiplies the index by the
  packed element width (bitstream width of the element type).
- Width hints for the multiply are widened to avoid truncating the scale
  factor (e.g., `* 128` was previously collapsing to zero).

After this, `wolf_emit.sv` shows:

```
assign _GEN_15 = _GEN_14[state * 4 +: 4];
```

## Current Status

With packed-array index scaling applied, the earlier mismatch is gone. The
cycle-218 segfault was previously avoided by clamping dynamic slice indices in
the emit path, but that clamp has now been removed (per request). The repro was
re-run without the clamp and still completed without a segfault:

```
make -C tests/data/xs-bugcase/CASE_003 run
```

Result:

```
[COVERAGE] line coverage 93.92% (2164/2304)
```

Coverage remains above the 90% threshold by running longer stimulus (20,000
cycles) in `tb.cpp`:

```
[COVERAGE] line coverage 93.92% (2164/2304)
```
