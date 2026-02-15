# XS Bugcase 005：Packed 数组动态索引降级错误

## 摘要
该用例最小化复现 `wire [31:0][4:0]` packed 数组在动态索引时被错误降级的问题：
- 在端口连接场景中，`arr[idx]` 被展开为 `flat[idx +: 5]`，缺少 `idx * 5`。
- 结果导致 `id_port` 与 `id_shift` 不一致，`bad` 置 1。

## 复现方式
```
make -C tests/data/xs-bugcase/CASE_005 run
```

## 期望行为
- ref：`idx=1` 时 `id_shift=1`、`id_port=1`、`sel=2`、`bad=0`。
- wolf（未修复）：`id_port` 与 `id_shift` 不一致，`bad=1`，并触发 ref/wolf mismatch。

## RTL 说明
`tests/data/xs-bugcase/CASE_005/rtl/PackedIndexBug.sv`
- `map` 为 32×5 packed 常量数组，与 `TLToAXI4_1` 的 `_GEN` 相同模式。
- `map[idx]` 同时用于：
  - 直接连到子模块端口 (`id_port`)；
  - 用于移位生成 one-hot (`id_shift`)。

该结构复现了 `TLToAXI4_1` 中“端口连接路径 + 计数选择路径”产生的索引不一致。

## 根因确认（convert vs emit）
已用 `wolf-sv-parser` 生成 GRH JSON 并定位问题来源，结论是 **convert 阶段**。
- JSON 路径：`build/xs_bugcase/CASE_005/confirm/grh.json`
- 在 `PackedIndexBug` 的 IR 中存在两条 `kSliceDynamic`：
  1) `__expr_0 = slice(__constfold__0_0, idx)`（loc: `rtl/PackedIndexBug.sv:29`）  
     对应端口连接路径，**缺少 `idx * 5`**。
  2) `map_idx = slice(__constfold__0_0, __expr_3)`（loc: `rtl/PackedIndexBug.sv:26`）  
     其中 `__expr_3 = idx * 5`，该路径是正确的。

由于 IR 已经在端口连接路径上丢失了 `*5`，emit 只是忠实输出 IR，问题归因于 **convert**。
