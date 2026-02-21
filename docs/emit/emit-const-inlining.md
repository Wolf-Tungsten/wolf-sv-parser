# Emit 常量内联/折叠完整清单（仅 emit，忽略 transform）

本文列出 `src/emit.cpp` 中**实际发生的常量内联、折叠或特殊处理**。默认策略是：`kConstant` 通常输出为显式赋值或变量初始化，**不在表达式中内联**；以下是例外/优化路径。

## 0) 基础路径与默认行为（不是内联，但影响内联）
- **常量来源**：`kConstant` 的 `constValue`。
- **默认策略**：`ExprTools.allowInlineLiteral = false`，因此大多数算术/逻辑表达式仍引用信号名，不内联常量。
- **字符串常量**：`addValueAssign()` 中 `ValueType::String` 会在声明处初始化（不是 `always_comb`）。

示例（默认）：  
```sv
wire [3:0] c0;
assign c0 = 4'd3;
assign y = a + c0; // 不直接写 a + 4'd3
```

---

## 1) 无尺寸常量补尺寸（内联场景的配套）
**路径**：`sizedLiteralIfUnsized()`  
**用途**：当需要内联时，把 `3` 这种无尺寸字面量补到目标宽度。  
**示例**：索引宽度 4 -> `3` 变成 `4'd3`。  

---

## 2) 常量符号扩展字面量（内联场景的配套）
**路径**：`signBitLiteralForConst()`  
**用途**：当发生符号扩展且操作数是常量时，生成常量符号位字面量。  
**示例**：  
```sv
// 常量 -1（4位）扩展到 8 位
{{4{1'b1}}, 4'shF}
```

---

## 3) writeport 的 updateCond 常量内联 + guard 简化
**路径**：`inlineConstExprFor()` + `isConstOne()` / `isAlwaysTrue()`  
**覆盖**：`kRegisterWritePort` / `kLatchWritePort` / `kMemoryWritePort`  
**用途**：`updateCond` 若为常量，直接写字面量；若恒真则移除 guard。  
**示例**：  
```sv
if (1'b1) begin reg <= next; end   // 被简化为 reg <= next;
```

---

## 4) writeport mask 常量折叠（全 0 / 全 1 / 逐位）
**路径**：`parseConstMaskBits()`  
**用途**：mask 全 0 -> 不生成写；全 1 -> 整字写；否则逐位写。  
**示例**：  
```sv
mask = 8'h00 -> (无写语句)
mask = 8'hFF -> reg <= data;
mask = 8'b1010 -> 只写 bit[1] / bit[3]
```

---

## 5) 仅作全 1 mask 的常量会被消隐
**路径**：`elidedConstValues`  
**用途**：常量只用于 writeport mask 且解析为全 1 -> 不再输出该常量 `assign`。  
**示例**：  
```sv
// const MASK = 8'hFF 仅作为 mask 使用
// -> 不再生成 assign MASK = 8'hFF;
```

---

## 6) inout 的 oe 常量优化（全 Z / 直连 / 逐位）
**路径**：`emitInoutAssign()`  
**用途**：`oe` 为常量时，全 0 直接 Z，全 1 直连输出，或逐位决定。  
**示例**：  
```sv
oe = 8'h00 -> assign io = {8{1'bz}};
oe = 8'hFF -> assign io = out;
```

---

## 7) inout 位级常量提取
**路径**：`emitInoutAssign()` 的 `bitExpr()`  
**用途**：逐位路径中，若 `oe/out` 是常量，生成 `((literal >> bit) & 1'b1)`。  
**示例**：  
```sv
((8'hA5 >> 3) & 1'b1)
```

---

## 8) 切片/索引常量内联
**路径**：`kSliceDynamic` / `kSliceArray` 中 `inlineConstExprFor()`  
**用途**：索引为常量时直接写字面量，避免 clamp 或额外逻辑。  
**示例**：  
```sv
assign y = x[4'd3 +: 4];
```

---

## 9) system task 字符串常量内联
**路径**：`kSystemTask` 的 `getSystemTaskArgExpr()`  
**用途**：字符串常量直接内联，避免初始化顺序影响 `$display/$fwrite`。  
**示例**：  
```sv
$display("hello");
```

---

## 10) DPI inline 表达式路径的常量内联
**路径**：`dpiInlineExpr()`（`allowInlineLiteral = true`）  
**用途**：当 `kDpicCall` return 被内联进 sink 时，常量可内联进算术/逻辑/拼接/切片等表达式。  
**示例**：  
```sv
reg <= dpi_func(a) + 8'd1;
assign y = (dpi_func(a) == 1'b0);
```

---

## 11) DPI inline 表达式支持的运算集合（常量可内联）
**路径**：`dpiInlineExpr()` 的 switch  
**覆盖**：  
- 比较：`== != === !== < <= > >=`  
- 位运算：`& | ^ ~^`  
- 移位：`<< >> >>>`  
- 算术：`+ - * / %`  
- 逻辑：`&& || !`  
- 归约：`& | ^ ~& ~| ~^`  
- 选择/赋值：`?:` / `assign`  
- 拼接/重复：`{}` / `{{rep{}}}`  
- 切片：静态/动态/数组切片  

---

## 12) 其他与常量相关的“输出细节”
这些不算内联表达式，但会改变常量呈现方式：  
- **字符串常量**：`formatConstLiteral()` 强制加引号并转义。  
- **常量未使用**：无 user 或被 `elidedConstValues` 标记时不输出 `assign`。  
```
// string s = "a\nb" -> "a\\nb"
```
