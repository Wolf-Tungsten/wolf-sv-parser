# HDLBits build/hdlbits 输出审阅（2026-02-01）

范围：`build/hdlbits/*/*.json` 与 `build/hdlbits/*/*.v`。对照 `docs/GRH-representation.md` 与 `docs/GRH-JSON-spec.md`。

## GRH/JSON 定义不符

1) `kInstance` 缺少 `inoutPortName`
- 规范要求 `inoutPortName` 始终存在（即便没有 inout，也应为空数组）。当前 `kInstance` 仅包含 `moduleName/inputPortName/outputPortName/instanceName`。
- 影响：JSON 消费端若按规范读取，会在解析 `kInstance` 时缺字段。
- 受影响文件（17 个）：
  - build/hdlbits/020/dut_020.json
  - build/hdlbits/021/dut_021.json
  - build/hdlbits/022/dut_022.json
  - build/hdlbits/023/dut_023.json
  - build/hdlbits/024/dut_024.json
  - build/hdlbits/025/dut_025.json
  - build/hdlbits/026/dut_026.json
  - build/hdlbits/027/dut_027.json
  - build/hdlbits/028/dut_028.json
  - build/hdlbits/043/dut_043.json
  - build/hdlbits/072/dut_072.json
  - build/hdlbits/103/dut_103.json
  - build/hdlbits/104/dut_104.json
  - build/hdlbits/106/dut_106.json
  - build/hdlbits/114/dut_114.json
  - build/hdlbits/118/dut_118.json
  - build/hdlbits/137/dut_137.json

2) `vals[].def`/`vals[].users` 与 GRH-JSON 约定不一致
- 现状：绝大多数 Value 的 `def` 为空字符串，但该 Value 确实由某个 op 产出；`users` 里也大量出现 `op: ""`。
- 影响：按规范 `def/users` 应通过 op 的 `sym` 定位，但 op `sym` 为空时无法解析，字段失去意义。
- 受影响范围：
  - `def` 为空且存在定义 op：除 `build/hdlbits/087/dut_087.json` 外，其余 160 个 JSON 都存在。
  - `users` 中 `op` 为空：除以下 5 个 JSON 外，其余 156 个 JSON 都存在：
    - build/hdlbits/001/dut_001.json
    - build/hdlbits/002/dut_002.json
    - build/hdlbits/081/dut_081.json
    - build/hdlbits/082/dut_082.json
    - build/hdlbits/087/dut_087.json
- 建议：要么为所有 op 生成稳定且唯一的 `sym` 并正确填写 `def/users`，要么在 JSON 中完全省略 `def/users` 字段以避免误导。

## 生成冗余（可优化项）

### Verilog 侧

1) 可消除的别名信号
- `build/hdlbits/111/dut_111.v`：`assign R = SW;` 仅用于一次复用，可直接把 `SW` 内联到 `__expr_10` 的 mux 里。

2) 频繁出现的常量中间线
- 71 个 .v 文件包含 `assign <tmp> = 1'b1;` 的常量中间线（多用于无条件更新的寄存器或常量输出）。这不是语义错误，但属于可内联的轻量冗余。
- 受影响文件清单：
  - build/hdlbits/001/dut_001.v
  - build/hdlbits/023/dut_023.v
  - build/hdlbits/024/dut_024.v
  - build/hdlbits/027/dut_027.v
  - build/hdlbits/030/dut_030.v
  - build/hdlbits/032/dut_032.v
  - build/hdlbits/036/dut_036.v
  - build/hdlbits/052/dut_052.v
  - build/hdlbits/081/dut_081.v
  - build/hdlbits/082/dut_082.v
  - build/hdlbits/083/dut_083.v
  - build/hdlbits/084/dut_084.v
  - build/hdlbits/085/dut_085.v
  - build/hdlbits/088/dut_088.v
  - build/hdlbits/089/dut_089.v
  - build/hdlbits/090/dut_090.v
  - build/hdlbits/091/dut_091.v
  - build/hdlbits/093/dut_093.v
  - build/hdlbits/094/dut_094.v
  - build/hdlbits/095/dut_095.v
  - build/hdlbits/096/dut_096.v
  - build/hdlbits/097/dut_097.v
  - build/hdlbits/098/dut_098.v
  - build/hdlbits/099/dut_099.v
  - build/hdlbits/100/dut_100.v
  - build/hdlbits/101/dut_101.v
  - build/hdlbits/103/dut_103.v
  - build/hdlbits/104/dut_104.v
  - build/hdlbits/105/dut_105.v
  - build/hdlbits/109/dut_109.v
  - build/hdlbits/110/dut_110.v
  - build/hdlbits/111/dut_111.v
  - build/hdlbits/112/dut_112.v
  - build/hdlbits/113/dut_113.v
  - build/hdlbits/114/dut_114.v
  - build/hdlbits/115/dut_115.v
  - build/hdlbits/116/dut_116.v
  - build/hdlbits/117/dut_117.v
  - build/hdlbits/118/dut_118.v
  - build/hdlbits/119/dut_119.v
  - build/hdlbits/120/dut_120.v
  - build/hdlbits/121/dut_121.v
  - build/hdlbits/122/dut_122.v
  - build/hdlbits/125/dut_125.v
  - build/hdlbits/126/dut_126.v
  - build/hdlbits/127/dut_127.v
  - build/hdlbits/128/dut_128.v
  - build/hdlbits/129/dut_129.v
  - build/hdlbits/130/dut_130.v
  - build/hdlbits/133/dut_133.v
  - build/hdlbits/134/dut_134.v
  - build/hdlbits/135/dut_135.v
  - build/hdlbits/136/dut_136.v
  - build/hdlbits/137/dut_137.v
  - build/hdlbits/138/dut_138.v
  - build/hdlbits/139/dut_139.v
  - build/hdlbits/140/dut_140.v
  - build/hdlbits/141/dut_141.v
  - build/hdlbits/142/dut_142.v
  - build/hdlbits/143/dut_143.v
  - build/hdlbits/147/dut_147.v
  - build/hdlbits/148/dut_148.v
  - build/hdlbits/150/dut_150.v
  - build/hdlbits/151/dut_151.v
  - build/hdlbits/154/dut_154.v
  - build/hdlbits/155/dut_155.v
  - build/hdlbits/156/dut_156.v
  - build/hdlbits/157/dut_157.v
  - build/hdlbits/159/dut_159.v
  - build/hdlbits/160/dut_160.v
  - build/hdlbits/162/dut_162.v

## 备注
- 其余检查项（op 类型合法性、slice 属性、register/memory 事件边沿长度、端口标记一致性）未发现明显不符。
