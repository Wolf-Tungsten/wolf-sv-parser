# 阶段13：CombAlwaysConverter if/case 支持

## 完成内容
- **ShadowFrame 栈模型**：`CombAlwaysConverter` 把原先的 `entry → ShadowState` 哈希表提升为「影子帧」栈。进入 `if/case` 前先复制当前帧作为 snapshot，子分支基于 snapshot 独立演化，结束后借助 `mergeShadowFrames` 逐目标对齐 bit-slices、拼出 `kMux`，保证 SSA + 阻塞赋值语义。
- **if/case 降级算法**：`if` 允许没有显式 `else`——通过 snapshot 形成隐式 fallback；`case` 亦可缺省 default，当所有 case 都未命中时回退到 snapshot，等效于用户手写的「先赋默认值再 case 覆盖」模式。真正缺失驱动时依旧触发 `comb always branch coverage incomplete`。
- **静态语义检查**：`unique/unique0` case 会在生成 mux 之前用 `EvalContext` 折出所有可计算的 case item，构建「位宽 + 十六进制文本」键值检测冲突，重复时立即报 `unique case items overlap on constant value ...`。`priority` 继续要求 coverage 完整，但可在后续阶段扩展成“至少命中一支”或运行期断言。
- **文档沉淀**：`docs/elaborate/process-always-comb.md` 记录 ShadowFrame 栈、merge 对齐策略、隐式 fallback 的语义以及 unique/priority 静态检查流程，便于后续在此基础上实现 loop、pattern case 等。
- **casex/casez**：对 `CaseStatementCondition::WildcardX/WildcardZ` 生成按位掩码比较，先 `kXor` 控制值与 item，再用由常量 `x/z` 位派生的 mask 通过 `kAnd` 清除忽略位，最后与零 `kEq` 得到匹配结果；若 item 不是常量则回退到普通 `case`。
- **测试增强**：新增 `comb_always_stage13_default_if`、`case_defaultless`、`casex`、`casez`、`unique_overlap`、`incomplete` 等模块，并在 `elaborate-comb-always` 集成测试里验证生成的 `kMux(cond, override, default)`、链式 case mux（含 wildcard）、unique 诊断与 latch 诊断，确保 shadow 栈逻辑覆盖多重控制流。

## 算法与数据结构细节
- **ShadowState**：每个 memo entry 挂一组 `WriteBackMemo::Slice`，进入分支时复制父帧的 `map` 以继承「默认值」。`rebuildShadowValue` 会按 MSB→LSB 拼接（必要时注入零），命名通过 `makeShadowOpName/ValueName` 保证可观测性。
- **mergeShadowFrames(cond, T, F)**：
  1. 汇总 `touched` 集合；若某 entry 仅在一边出现则报 latch。
  2. 对 coverage 中每个 entry，调用 `rebuildShadowValue` 取 true/false 完整 value，创建 `kMux(cond, true, false)`，再用 `buildFullSlice` 写回合并帧。
  3. 合并后的帧继承 false-frame 的 `map`，但 `touched` union(T,F)，方便后续嵌套控制流继续复用。
- **CaseBranch 链**：逆序折叠，初始累积帧为 default/snapshot；每个 case 项的 match value 由 `kEq` + `kOr` 链构成。这样可以保持「后写覆盖先写」的 SystemVerilog 语义，同时保证合流处只有单一 mux。
- **Unique 检查**：利用 `EvalContext` 的整数求值能力（忽略含 X/Z 的项）并以 `<bitwidth>:<hex-text>` 为 key，借助 `unordered_map` 检查重复。若 type 含有 unknown 或无法静态求值则跳过，使检查保持 conservative。

## 测试记录
- `cd build && ctest`

## 测试记录
- `cd build && ctest`
