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

## Warnings (clean run on Feb 12, 2026)

Collected after:

```
make -C tests/data/xs-bugcase/CASE_003 clean
make -C tests/data/xs-bugcase/CASE_003 run
```

### slang

- 4 warnings reported by slang (details not printed in the log).

### convert

Repeated warning: `Skipping sequential write without edge-sensitive timing control`.

```
rtl/NewPipelineConnectPipe_21.sv:115:9
rtl/VSegmentUnit.sv:3186:9  instMicroOpValid = 1'h0;
rtl/VSegmentUnit.sv:3187:9  enqPtr_flag = 1'h0;
rtl/VSegmentUnit.sv:3188:9  enqPtr_value = 3'h0;
rtl/VSegmentUnit.sv:3189:9  deqPtr_flag = 1'h0;
rtl/VSegmentUnit.sv:3190:9  deqPtr_value = 3'h0;
rtl/VSegmentUnit.sv:3191:9  stridePtrReg_value = 3'h0;
rtl/VSegmentUnit.sv:3192:9  segmentIdx = 8'h0;
rtl/VSegmentUnit.sv:3193:9  fieldIdx = 4'h0;
rtl/VSegmentUnit.sv:3194:9  segmentOffset = 64'h0;
rtl/VSegmentUnit.sv:3195:9  splitPtr_flag = 1'h0;
rtl/VSegmentUnit.sv:3196:9  splitPtr_value = 3'h0;
rtl/VSegmentUnit.sv:3197:9  latchVaddr = 64'h0;
rtl/VSegmentUnit.sv:3198:9  latchVaddrDup = 64'h0;
rtl/VSegmentUnit.sv:3199:9  fofBuffer_fuOpType = 9'h0;
rtl/VSegmentUnit.sv:3200:9  fofBuffer_vecWen = 1'h0;
rtl/VSegmentUnit.sv:3201:9  fofBuffer_v0Wen = 1'h0;
rtl/VSegmentUnit.sv:3202:9  fofBuffer_vlWen = 1'h0;
rtl/VSegmentUnit.sv:3203:9  fofBuffer_vpu_vill = 1'h0;
rtl/VSegmentUnit.sv:3204:9  fofBuffer_vpu_vma = 1'h0;
rtl/VSegmentUnit.sv:3205:9  fofBuffer_vpu_vta = 1'h0;
rtl/VSegmentUnit.sv:3206:9  fofBuffer_vpu_vsew = 2'h0;
rtl/VSegmentUnit.sv:3207:9  fofBuffer_vpu_vlmul = 3'h0;
rtl/VSegmentUnit.sv:3208:9  fofBuffer_vpu_specVill = 1'h0;
rtl/VSegmentUnit.sv:3209:9  fofBuffer_vpu_specVma = 1'h0;
rtl/VSegmentUnit.sv:3210:9  fofBuffer_vpu_specVta = 1'h0;
rtl/VSegmentUnit.sv:3211:9  fofBuffer_vpu_specVsew = 2'h0;
rtl/VSegmentUnit.sv:3212:9  fofBuffer_vpu_specVlmul = 3'h0;
rtl/VSegmentUnit.sv:3213:9  fofBuffer_vpu_vm = 1'h0;
rtl/VSegmentUnit.sv:3214:9  fofBuffer_vpu_vstart = 8'h0;
rtl/VSegmentUnit.sv:3215:9  fofBuffer_vpu_frm = 3'h0;
rtl/VSegmentUnit.sv:3216:9  fofBuffer_vpu_fpu_isFpToVecInst = 1'h0;
rtl/VSegmentUnit.sv:3217:9  fofBuffer_vpu_fpu_isFP32Instr = 1'h0;
rtl/VSegmentUnit.sv:3218:9  fofBuffer_vpu_fpu_isFP64Instr = 1'h0;
rtl/VSegmentUnit.sv:3219:9  fofBuffer_vpu_fpu_isReduction = 1'h0;
rtl/VSegmentUnit.sv:3220:9  fofBuffer_vpu_fpu_isFoldTo1_2 = 1'h0;
rtl/VSegmentUnit.sv:3221:9  fofBuffer_vpu_fpu_isFoldTo1_4 = 1'h0;
rtl/VSegmentUnit.sv:3222:9  fofBuffer_vpu_fpu_isFoldTo1_8 = 1'h0;
rtl/VSegmentUnit.sv:3223:9  fofBuffer_vpu_vxrm = 2'h0;
rtl/VSegmentUnit.sv:3224:9  fofBuffer_vpu_vuopIdx = 7'h0;
rtl/VSegmentUnit.sv:3225:9  fofBuffer_vpu_lastUop = 1'h0;
rtl/VSegmentUnit.sv:3226:9  fofBuffer_vpu_nf = 3'h0;
rtl/VSegmentUnit.sv:3227:9  fofBuffer_vpu_veew = 2'h0;
rtl/VSegmentUnit.sv:3228:9  fofBuffer_vpu_isReverse = 1'h0;
rtl/VSegmentUnit.sv:3229:9  fofBuffer_vpu_isExt = 1'h0;
rtl/VSegmentUnit.sv:3230:9  fofBuffer_vpu_isNarrow = 1'h0;
rtl/VSegmentUnit.sv:3231:9  fofBuffer_vpu_isDstMask = 1'h0;
rtl/VSegmentUnit.sv:3232:9  fofBuffer_vpu_isOpMask = 1'h0;
rtl/VSegmentUnit.sv:3233:9  fofBuffer_vpu_isMove = 1'h0;
rtl/VSegmentUnit.sv:3234:9  fofBuffer_vpu_isDependOldVd = 1'h0;
rtl/VSegmentUnit.sv:3235:9  fofBuffer_vpu_isWritePartVd = 1'h0;
rtl/VSegmentUnit.sv:3236:9  fofBuffer_vpu_isVleff = 1'h0;
rtl/VSegmentUnit.sv:3237:9  fofBuffer_vpu_maskVecGen = 16'h0;
rtl/VSegmentUnit.sv:3238:9  fofBuffer_vpu_sew8 = 1'h0;
rtl/VSegmentUnit.sv:3239:9  fofBuffer_vpu_sew16 = 1'h0;
rtl/VSegmentUnit.sv:3240:9  fofBuffer_vpu_sew32 = 1'h0;
rtl/VSegmentUnit.sv:3241:9  fofBuffer_vpu_sew64 = 1'h0;
rtl/VSegmentUnit.sv:3242:9  fofBuffer_pdest = 8'h0;
rtl/VSegmentUnit.sv:3243:9  fofBuffer_robIdx_flag = 1'h0;
rtl/VSegmentUnit.sv:3244:9  fofBuffer_robIdx_value = 9'h0;
rtl/VSegmentUnit.sv:3245:9  fofBuffer_perfDebugInfo_eliminatedMove = 1'h0;
rtl/VSegmentUnit.sv:3246:9  fofBuffer_perfDebugInfo_renameTime = 64'h0;
rtl/VSegmentUnit.sv:3247:9  fofBuffer_perfDebugInfo_dispatchTime = 64'h0;
rtl/VSegmentUnit.sv:3248:9  fofBuffer_perfDebugInfo_enqRsTime = 64'h0;
rtl/VSegmentUnit.sv:3249:9  fofBuffer_perfDebugInfo_selectTime = 64'h0;
rtl/VSegmentUnit.sv:3250:9  fofBuffer_perfDebugInfo_issueTime = 64'h0;
rtl/VSegmentUnit.sv:3251:9  fofBuffer_perfDebugInfo_runahead_checkpoint_id = 64'h0;
rtl/VSegmentUnit.sv:3252:9  fofBuffer_perfDebugInfo_tlbFirstReqTime = 64'h0;
rtl/VSegmentUnit.sv:3253:9  fofBuffer_perfDebugInfo_tlbRespTime = 64'h0;
rtl/VSegmentUnit.sv:3254:9  fofBuffer_debug_seqNum_seqNum = 56'h0;
rtl/VSegmentUnit.sv:3255:9  fofBuffer_debug_seqNum_uopIdx = 8'h0;
rtl/VSegmentUnit.sv:3256:9  fofBufferValid = 1'h0;
rtl/VSegmentUnit.sv:3257:9  state = 4'h0;
rtl/VSegmentUnit.sv:3258:9  curPtr = 1'h0;
rtl/VSegmentUnit.sv:3259:9  isMisalignReg = 1'h0;
rtl/VSegmentUnit.sv:3260:9  notCross16ByteReg = 1'h0;
rtl/VSegmentUnit.sv:3261:9  combinedData = 64'h0;
rtl/VSegmentUnit.sv:3262:9  lowPagePaddr = 48'h0;
rtl/VSegmentUnit.sv:3263:9  triggerDebugMode = 1'h0;
rtl/VSegmentUnit.sv:3264:9  triggerBreakpoint = 1'h0;
rtl/VSegmentUnit.sv:3265:9  last_REG = 1'h0;
rtl/VSegmentUnit.sv:3266:9  nextBaseVaddr_r = 64'h0;
```

### clang (RTL)

```
rtl/VSegmentUnit.sv:1021:46: warning: if statement has empty body [-Wempty-body]
rtl/VSegmentUnit.sv:1027:46: warning: if statement has empty body [-Wempty-body]
rtl/VSegmentUnit.sv:1035:46: warning: if statement has empty body [-Wempty-body]
rtl/VSegmentUnit.sv:1041:46: warning: if statement has empty body [-Wempty-body]
```

### Verilator (wolf_emit.sv)

```
%Warning-WIDTHTRUNC: wolf_emit.sv:3433:27 Bit extraction of var[63:0] requires 6 bit index, not 7 bits.
%Warning-WIDTHCONCAT: wolf_emit.sv:3459:67 Unsized numbers/parameters not allowed in concatenations.
%Warning-WIDTHCONCAT: wolf_emit.sv:3459:66 Unsized numbers/parameters not allowed in replications.
%Warning-WIDTHTRUNC: wolf_emit.sv:3461:27 Bit extraction of var[1023:0] requires 10 bit index, not 11 bits.
%Warning-WIDTHCONCAT: wolf_emit.sv:3871:63 Unsized numbers/parameters not allowed in concatenations.
%Warning-WIDTHCONCAT: wolf_emit.sv:3871:62 Unsized numbers/parameters not allowed in replications.
%Warning-WIDTHTRUNC: wolf_emit.sv:3873:27 Bit extraction of var[1023:0] requires 10 bit index, not 11 bits.
```

### Verilator C++ headers (toolchain noise)

Multiple `-Wsign-compare` warnings in `verilated_funcs.h` and
`verilated_types.h` due to comparisons between `int32_t` and `size_t` inside
Verilator helper templates.
