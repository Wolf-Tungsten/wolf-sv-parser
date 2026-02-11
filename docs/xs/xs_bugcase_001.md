# XS Bugcase 001: SRAM bit-write shift width mismatch

## Summary

`wolf_emit.sv` fails to compile with Verilator due to illegal bit-selects on scalar
signals inside `array_128x76` (used by
`sram_array_1p128x76m1s1h0l1b_bpu_ittage_bank1`).
The emitted code writes `Memory[RW0_addr][bit] <= __expr_X[bit];` but `__expr_X`
is declared as a scalar `wire`, so `__expr_X[bit]` is out-of-range when `bit != 0`.

## Reproduction

```
make -C tests/data/xs-bugcase/CASE_001 run
```

Error excerpt (from `build/xs_bugcase/CASE_001/wolf/wolf_emit.sv`):

```
%Error: .../wolf_emit.sv:1743:39: Illegal bit or array select; type does not have a bit range
  Memory[RW0_addr][1] <= __expr_12[1];
```

## Analysis

In the emitted `array_128x76` module:

```
assign __expr_11 = RW0_wdata[1];
assign __expr_12 = __expr_11 << 1; // __expr_12 is scalar
...
Memory[RW0_addr][1] <= __expr_12[1];
```

Because `__expr_12` is declared as `wire __expr_12;` (width 1), Verilator
rejects `__expr_12[1]`. This pattern repeats for all bits.

### Why `kShl(write.value, bitIndex)` exists

The converter normalizes all memory writes into a single
`kMemoryWritePort(updateCond, addr, data, mask)` form. For a bit-select write such as:

```
Memory[addr][i] <= value; // value is 1-bit
```

it builds:

- `mask = 1 << i` (only update bit `i`)
- `data = value << i` (place the 1-bit value into bit position `i`)

This lets the emitter use one generic masked-write implementation for
bit/partial/full writes.

### What `kMemoryWritePort` operands mean (detail)

Conceptually, a memory write in the IR is normalized into:

```
if (updateCond) begin
  // For each bit i in [0..memWidth-1]
  if (mask[i]) Memory[addr][i] <= data[i];
end
```

So each operand has a concrete, mechanical meaning:

- **`updateCond` (1-bit)**: overall write enable. If this is 0, the write does
  nothing. In the SRAM code it corresponds to `RW0_en & RW0_wmode` (and any
  other gating that the source code adds).
- **`addr`**: the memory row index. For `Memory[RW0_addr]` this is simply
  `RW0_addr`.
- **`data` (memWidth bits)**: the *entire* data word to be written *after
  alignment*. It is not the original RHS expression; it is a normalized word
  where the bits are already placed in their target positions.
- **`mask` (memWidth bits)**: per-bit write enable. `mask[i]=1` means
  `Memory[addr][i]` will be updated from `data[i]`; `mask[i]=0` means that bit
  stays unchanged.

#### Example 1: full write

Original SV:

```
Memory[addr] <= wdata; // wdata is memWidth bits
```

Normalized:

- `updateCond = 1`
- `addr = addr`
- `data = wdata`
- `mask = {memWidth{1'b1}}` (all ones)

#### Example 2: bit write (the failing case)

Original SV:

```
Memory[addr][i] <= bitval; // bitval is 1-bit
```

Normalized:

- `updateCond = 1` (or gating from the source)
- `addr = addr`
- `data = bitval << i`   (place the 1-bit into position `i`)
- `mask = 1 << i`        (enable only bit `i`)

The important detail is that **`data` must be memWidth bits**, because later
the emitter does `data[i]`. If `data` is still 1-bit wide, `data[i]` is
illegal except when `i==0`.

#### Example 3: range write

Original SV:

```
Memory[addr][hi:lo] <= slice; // slice width = hi-lo+1
```

Normalized:

- `updateCond = 1`
- `addr = addr`
- `data = slice << lo`
- `mask = ((1 << (hi-lo+1)) - 1) << lo`

Again, `data` and `mask` must be full width so that the generic per-bit loop
works.

### What input triggers the bug

The pattern above is exactly what appears in `array_128x76.sv`:

```
if (RW0_en & RW0_wmask[i] & RW0_wmode)
  Memory[RW0_addr][i] <= RW0_wdata[i];
```

Each bit write goes through the `WriteSliceKind::BitSelect` path, which uses
`kShl(write.value, bitIndex)` without a width hint. Since `write.value` is
1-bit, the shift result remains 1-bit, but the emitter later indexes it as if
it were `memWidth` wide, producing `__expr_*[i]` on a scalar and causing the
illegal bit-select errors.

The root cause is in the memory write lowering for bit-select writes in
`src/convert.cpp` (around the `WriteSliceKind::BitSelect` path). For each bit,
`data` is constructed as:

```
data = kShl(write.value, bitIndex)
```

but `makeOperation` is used without a width hint. The resulting `kShl` value
keeps the 1-bit width of `write.value`, even though the shift intends to place
that bit into a `memWidth`-wide vector. Later, the emitter assumes the data is
`memWidth` wide and indexes it by `bit` in the memory write expansion.

## Fix Outcome: Input/Output Shape (Before vs After)

Below is the concrete shape change in the IR and emitted SV for the
`Memory[addr][i] <= RW0_wdata[i]` pattern in `array_128x76`.

### IR Shape

**Before (broken):**

- `data` width: **1**
- `mask` width: **32** (truncated for `i >= 32`)

```
kMemoryWritePort(updateCond, addr,
                 data = kShl(bitval, i)   // 1-bit
                 mask = kShl(1, i)        // 32-bit
)
```

**After (fixed):**

- `data` width: **memWidth (76)**
- `mask` width: **memWidth (76)**

```
kMemoryWritePort(updateCond, addr,
                 data = kShl(bitval, i)   // 76-bit
                 mask = kShl(1, i)        // 76-bit
)
```

### Emitted SV Shape

**Before (broken):**

```
wire __expr_11;             // 1-bit
assign __expr_12 = __expr_11 << i;  // 1-bit result
Memory[addr][i] <= __expr_12[i];    // illegal when i != 0
```

**After (fixed):**

```
wire __expr_11;             // 1-bit
assign __expr_12 = {{75{1'b0}}, __expr_11} << i; // 76-bit result
Memory[addr][i] <= __expr_12[i];                 // valid for all i
```

For indexed part-select masks (e.g. `[(base +: width)]`), the `1` literal used
to build `((1 << width) - 1)` is also now `memWidth`-sized, ensuring mask bits
above 31 are preserved.

## Fix Impact Summary (IR + Emit)

### GRH IR Impact

- **No new op kinds** were introduced; the IR graph structure is unchanged.
- **Width changes only**:
  - `kMemoryWritePort` operands `data`/`mask` are now **memWidth** instead of
    1/32 bit.
  - Constants that encode per‑bit masks are now **memWidth‑sized literals**
    (e.g. `76'h...` instead of `32'h...`).
- **No new attributes** were added to GRH ops; the change is strictly width
  hints/constant widths in existing nodes.

### Emit Impact

- **Only the `kShl/kLShr/kAShr` emission path changed**.
  - The emitter now **extends the left operand to the result width** before
    shifting, emitting an explicit zero/sign extension in SV (e.g.
    `{{75{1'b0}}, lhs} << i`) when needed.
  - This is a *codegen* adjustment; it does **not** change IR semantics or add
    IR ops.

## Fix Suggestions

1. **Widen `kShl` for bit-select writes (preferred).**
   In `src/convert.cpp` under `WriteSliceKind::BitSelect`, build `mask` and
   `data` with `makeOperationWithWidth` and a width hint of `memWidth`:

   - `mask = kShl(one, bitIndex)` with width `memWidth`
   - `data = kShl(write.value, bitIndex)` with width `memWidth`

   This matches FIRRTL/IR intent (left shift widens), yields `wire [memWidth-1:0]`
   for `__expr_*`, and makes `__expr_*[bit]` legal.

2. **Add width sanity check.**
   Emit a diagnostic if `kMemoryWritePort` data width does not match `memWidth`
   and the mask is not all-ones, to catch similar issues earlier.

**Constraint:** The fix should not introduce unnecessary extra IR operations.
Prefer minimal-width hints or metadata changes that preserve the existing IR
structure and keep the generated IR concise.

## Validation Plan

- Re-run `make -C tests/data/xs-bugcase/CASE_001 run` after the fix.
- Ensure `wolf_emit.sv` compiles and the ref vs wolf comparison runs.
