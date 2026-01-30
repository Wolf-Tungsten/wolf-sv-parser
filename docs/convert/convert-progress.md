# Convert 开发进展跟踪

## 整体目标

现有 Elaborate 过于臃肿无法维护、代码架构缺少高性能考虑，需要进行迁移重构，形成新的 Convert

Convert 在功能上与 Elaborate 等价，由 Slang AST 构建 GRH 表示

架构与流程上要结构清晰、易维护、面向性能优化

## 文档 SOP

在开发的过程中不断维护三个文档以保证工作可追踪、架构可理解：

- docs/convert/convert-progress.md（本文档）：进展文档，增量式记载每个 step 的计划与实施情况，在分割线后每次增加一个 STEP XXXX 章节；如果后续步骤推翻了前序设计，则前序文档不删除，以markdown删除线形式标注。

- docs/convert/convert-architecture.md：架构文档，以静态、空间化、自顶向下结构描述当前 Convert 的架构组件、连接关系；该文档与代码保持强一致性，每次更新代码后都需要及时更新文档，不保留增量内容，变动直接覆盖，并且总是保持自顶向下结构。

- docs/convert/convert-workflow.md：工作流文档，以动态、时间顺序、运行时从前到后描述 Convert 的工作流程；该文档与代码保持强一致性，每次更新代码后都需要及时更新文档，不保留增量内容，变动直接覆盖，并且总是保持运行时从前到后顺序。

## 架构期望

充分利用 systemverilog 变量静态化的特点，即所有变量均预先定义，运行时不创建新变量的特质

- 先收集所有变量

- 再扫描语句确定变量的读写关系

- 确定读写关系后逐步确定变量的类型（wire/reg/mem）

- 由变量构建 Value，读写关系构建 Op，最终形成 Graph 和 Netlist

------

## STEP 0001 - 更新 GRH 表示定义（kMemory 端口与四态逻辑）

目标：
- 精简 kMemory 读写端口类型，只保留指定读口/写口
- 明确读口复位作用于读寄存器，写口不复位
- 明确 GRH 支持四态逻辑

实施：
- 已更新 `docs/GRH-representation.md` 的 Operation 列表与 kMemory 端口定义
- 已补充四态逻辑语义说明

完成情况：已完成

## STEP 0002 - 制定 Convert 新架构与工作流方案

目标：
- 基于 slang AST 能力、GRH 表示约束与 Elaborate 现状，提出新的 Convert 架构与流程
- 明确分阶段数据模型与职责划分，降低 memo 粘连

实施：
- 已更新 `docs/convert/convert-architecture.md`，给出 Convert 的静态组件与数据模型
- 已更新 `docs/convert/convert-workflow.md`，给出运行时从入口到 Netlist 输出的流程
- 已补充多顶层模块约束、复杂 always/控制流识别、层次保留与参数特化策略
- 已补充可控调试日志接口的预留说明

完成情况：已完成

## STEP 0003 - 建立 Convert 静态骨架代码

目标：
- 落地 Convert 核心类型/接口的代码骨架
- 同步更新架构与工作流文档中的代码落位

实施：
- 新增 `include/convert.hpp` 与 `src/convert.cpp` 的骨架定义
- 新增 `convert` 静态库目标并接入 `CMakeLists.txt`
- 更新 `docs/convert/convert-architecture.md` 与 `docs/convert/convert-workflow.md`

完成情况：已完成

## STEP 0004 - 接入 Convert 到 CLI 与构建流程

目标：
- 在 `wolf-sv-parser` CLI 中接入 Convert 入口，便于测试调试
- 将 Convert 链接到可执行文件构建流程

实施：
- `src/main.cpp` 增加 `--use-convert` 与 Convert 日志相关选项
- `CMakeLists.txt` 将 `convert` 链接到 `wolf-sv-parser`

完成情况：已完成

## STEP 0005 - ModulePlanner 数据结构设计与骨架更新

目标：
- 细化 ModulePlanner 相关数据结构设计，避免性能瓶颈
- 支持大规模输入的高效遍历与后续并行化

实施：
- `docs/convert/convert-architecture.md` 补充 ModulePlanner 数据结构与并行化策略
- `docs/convert/convert-workflow.md` 标注可并行模块计划阶段
- `include/convert.hpp`/`src/convert.cpp` 更新 PlanSymbolTable 与索引化结构骨架
- 进一步明确并行化方法（任务单元、调度模型、去重与阶段边界）
- 新增 PlanCache/PlanTaskQueue 并接入 ConvertContext 骨架

完成情况：已完成

## STEP 0006 - 建立 ConvertContext 与 Plan 数据结构

目标：
- 明确 ConvertContext 与 Plan 相关的核心数据结构
- 为后续 Pass 实现提供最小可用骨架

实施：
- `include/convert.hpp` 新增 LoweringPlan/WriteBackPlan/PlanArtifacts 骨架
- `PlanEntry` 扩展以持有 artifacts

完成情况：已完成

## STEP 0007 - Context Setup 实现与文档对齐

目标：
- 实现 ConvertContext 初始化与缓存复位逻辑
- 同步 Context Setup 的文档描述

实施：
- `PlanCache::clear` 与 `PlanTaskQueue::reset` 实现并在 convert 时调用
- `ConvertContext` 初始化时设置 compilation/root/options/diagnostics/logger
- 文档补充 Context Setup 生命周期说明
- 细化 `PlanCache` 的 PlanArtifacts 读写接口
- 增加 PlanArtifacts 无拷贝访问与状态校验逻辑

完成情况：已完成

## STEP 0008 - Pass1 SymbolCollector 落地

目标：
- 实现 Pass1（SymbolCollectorPass），收集端口/信号/实例信息并填充 ModulePlan
- 在 Pass1 遍历实例时投递子模块 PlanKey，打通任务发现流程

实施：
- `ModulePlanner::plan` 补充端口、信号、实例收集逻辑
- 递归遍历 instance array 与 generate block，覆盖嵌套实例
- 基于实例体记录 `InstanceInfo`，并向 `PlanTaskQueue` 投递子模块任务
- `ConvertDriver::convert` 跑通顶层实例的 Pass1 队列调度与 PlanCache 写入

完成情况：已完成

## STEP 0009 - Pass2 TypeResolver 落地

目标：
- 实现 Pass2（TypeResolverPass），计算端口/信号的位宽、签名、packed/unpacked 维度与 memory 行数
- 将 Pass2 接入 Convert 主流程，保证 ModulePlan 类型信息完整

计划：
- 回扫 PortSymbol/NetSymbol/VariableSymbol 获取 `slang::ast::Type` 并更新 ModulePlan
- 解析 packed/unpacked 维度并计算 `memoryRows`，补充诊断与边界处理
- 更新架构与工作流文档保持一致

实施：
- `src/convert.cpp` 新增 TypeResolverPass 与类型解析辅助逻辑
- ConvertDriver 在 Pass1 后调用 TypeResolverPass 更新 ModulePlan
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0010 - Pass3 RWAnalyzer 落地

目标：
- 实现 Pass3（RWAnalyzerPass），建立读写关系与控制域语义
- 将 Pass3 接入 Convert 主流程，更新 `ModulePlan.rwOps`/`ModulePlan.memPorts`

计划：
- 解析过程块/连续赋值 AST，分类控制域并收集读写访问
- 识别 memory 访问并生成 `MemoryPortInfo`
- 更新架构与工作流文档保持一致

实施：
- `src/convert.cpp` 新增 RWAnalyzerPass 与 AST 遍历/控制域分类逻辑
- ConvertDriver 在 Pass2 后调用 RWAnalyzerPass 更新 ModulePlan
- 新增 `convert-rw-analyzer` HDLBits 测试与 CMake 注册
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0011 - Pass4 ExprLowerer 落地

目标：
- 实现 Pass4（ExprLowererPass），将 RHS 表达式降级为 LoweringPlan
- 接入 Convert 主流程并补充测试

实施：
- `include/convert.hpp` 增补 LoweringPlan 节点结构与 ExprLowererPass 声明
- `src/convert.cpp` 实现 ExprLowererPass，覆盖常见 unary/binary/conditional/concat/replicate/select
- ConvertDriver 在 Pass3 后生成并缓存 `loweringPlan`
- 新增 `convert-expr-lowerer` 测试与 fixture
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0012 - Pass5 StmtLowerer 落地

目标：
- 实现 Pass5（StmtLowererPass），降级语句与控制流并生成写回意图
- 扩展 LoweringPlan 以承载 guard 与写回意图
- 增补测试覆盖 if/else 与连续赋值场景

实施：
- `include/convert.hpp` 新增 WriteIntent 与 `LoweringPlan.writes`
- `src/convert.cpp` 实现 StmtLowererPass，支持 if/else guard 叠加与写回意图生成
- ConvertDriver 接入 Pass5
- 新增 `convert-stmt-lowerer` 测试与 fixture
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0013 - Pass5 If/Else 链路完善

目标：
- 支持多条件 if/else if/else 语句链，保持 guard 组合语义正确
- 对 pattern 条件给出明确诊断与回退策略
- 增补 if/else 链路的单元测试与 fixture

计划：
- 扩展 StmtLowerer 的 Conditional 处理，支持多条件链遍历
- Guard 组合规则明确化（`base && cond` / `base && !cond`）
- 新增 `convert-stmt-lowerer` 对应用例与断言
- 更新 workflow/architecture 文档描述

实施：
- StmtLowerer 支持多条件组合 guard 与 else/else-if 链路
- 新增 if/else 链路测试覆盖

完成情况：已完成

## STEP 0014 - Pass5 Case 语句支持

目标：
- 支持普通 case/casez/casex 的 guard 构建与分支合并
- 初期不强制 unique/priority，仅记录诊断提示
- 增补 case 测试与 fixture

计划：
- 引入 case 比较 lowering（== / wildcard compare）
- 将每个分支 guard 累积到 LoweringPlan.writes
- default 分支与 full-case 行为明确
- 更新 workflow/architecture 文档描述

实施：
- StmtLowerer 支持 case/casez/casex 的 guard 构建与 priorMatch 合并
- 新增 case/casez/casex 测试覆盖

完成情况：已完成

## STEP 0015 - Pass5 可静态展开的循环支持

目标：
- 支持可静态求值的 for/repeat/foreach 循环展开
- 不可静态求值的循环给出 TODO 诊断并保持安全回退
- 增补循环相关测试与 fixture

计划：
- 借助 slang 常量求值能力判断循环边界
- 仅对确定次数/范围的循环做语句展开
- 保持 guard/写回语义与展开顺序一致
- 更新 workflow/architecture 文档描述

实施：
- StmtLowerer 支持 repeat/for/foreach 的静态展开与安全回退
- 新增 repeat/for/foreach 测试覆盖

完成情况：已完成

## STEP 0016 - Pass5 casez/casex 通配匹配语义

目标：
- 为 casez/casex 实现 X/Z 通配匹配语义，避免退化成 kEq
- 对无法处理的 case item 给出清晰诊断并回退
- 增补 casez/casex 通配位的测试覆盖

计划：
- 在 StmtLowerer 的 case item match 中区分 case/casez/casex
- 对常量 case item 生成 mask：
  - casez: Z / ? -> mask=0
  - casex: X / Z / ? -> mask=0
  - 其余位 mask=1
- match 公式改为 `(control & mask) == (item & mask)`，组合逻辑仍按 priorMatch
- 非常量 item 或无法提取 X/Z 时发出诊断，回退到 kCaseEq
- 新增 casez/casex 夹带 X/Z/? 的 fixture 与断言，并更新 workflow/architecture 说明

实施：
- GRH 新增 `kCaseEq/kCaseNe/kWildcardEq/kWildcardNe`，并在 emit/verify/const_fold 接入
- Convert 的二元运算映射拆分 case/wildcard equality 到新 op
- StmtLowerer casez/casex 基于常量 item 生成 mask，使用 `kAnd + kEq` 构建 match
- 普通 case 在 2-state + 常量无 X/Z 时用 `kEq`，否则回退 `kCaseEq` 并发 warning
- 非常量 casez/casex 发出 warning 并回退 `kCaseEq`

完成情况：已完成

## STEP 0017 - Pass5 case inside 语义支持

目标：
- 支持 `case inside` 的匹配语义并参与 guard 组合
- 覆盖 range/inside list 等 item 形式的匹配
- 明确不支持场景的诊断策略

计划：
- 在 StmtLowerer 的 case 分支中实现 `CaseStatementCondition::Inside` 的 match lowering
- 逐 item 生成 inside match，并与 priorMatch 组合 guard
- 对无法转换的 item 直接报错并跳过该 item
- 增补 case inside 相关测试与 fixture

实施：
- StmtLowerer 支持 `CaseStatementCondition::Inside`，逐 item 生成 match 并复用 priorMatch
- 普通 item：integral 使用 `kWildcardEq`，非 integral 使用 `kEq`
- `ValueRange` 支持 `[a:b]`、`[a +/- b]`、`[a +%- b]` 展开为 `kGe/kLe` 组合
- 新增 case inside fixture，并将测试语言版本升级到 SV2023 以解析 `+/-`、`+%-`

完成情况：已完成

## STEP 0018 - Pass5 不支持语法的错误诊断收敛

目标：
- pattern 条件（if/else-if）与 pattern case 明确报错，不再回退生成无 guard 语义
- ~~while/do-while/forever 直接报错并停止对其 body 的降级~~（已被 STEP 0026 替代）

计划：
- 将相关分支的诊断从 todo 升级为 error
- ~~对 pattern 条件/PatternCase/while/do/forever 采用 “报错 + 跳过” 策略~~（已被 STEP 0026 替代）
- 更新 workflow/architecture 说明不支持范围
- 增补错误路径测试

实施：
- StmtLowerer 将 pattern condition 与 pattern case 升级为 error 并直接跳过分支
- ~~while/do-while/forever 直接报错并停止对 body 的降级~~（已被 STEP 0026 替代）
- 新增 `convert-stmt-lowerer-errors` 测试与 fixture 覆盖错误路径
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0019 - Pass5 循环展开上限提升

目标：
- 扩大静态展开的迭代上限，解决 4096 不够大的问题
- 保持可配置或可调整的上限策略

计划：
- 将 `kMaxLoopIterations` 调整为更大的默认值，或迁移到 ConvertOptions 配置
- 更新文档对循环展开上限的说明
- 增补大迭代次数的 repeat/for/foreach 测试

实施：
- `ConvertOptions` 新增 `maxLoopIterations`（默认 65536）并接入 StmtLowerer 的循环展开判定
- StmtLowerer 以配置上限替代硬编码 4096
- 新增大迭代次数 repeat/for/foreach fixture 与测试断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0020 - Pass5 位选/范围选 LHS 支持

目标：
- 支持 bit-select / range-select LHS 的部分赋值
- 生成可被后续 WriteBack 处理的写回意图

计划：
- 扩展 WriteIntent 以携带部分赋值的切片信息（索引/范围表达式）
- 在 StmtLowerer 解析 LHS 并记录 slice 元数据
- 更新后续 Pass（WriteBack/GraphAssembly）对部分写回的处理
- 增补位选/范围选 LHS 的测试与 fixture

实施：
- `WriteIntent` 新增 `slices`/`WriteSlice`/`WriteRangeKind` 描述 LHS 切片信息
- WriteBack/GraphAssembly 仍未落地，切片信息已预留供后续 Pass 消费
- StmtLowerer 在解析 LHS 时收集 bit-select 与 range-select（含 indexed up/down）
- 新增 `stmt_lowerer_lhs_select` fixture 与断言覆盖切片类型与范围选择种类
- `docs/convert/convert-architecture.md` 与 `docs/convert/convert-workflow.md` 同步更新

完成情况：已完成

## STEP 0021 - Pass5 静态可求值 break/continue 支持

目标：
- 在可静态求值的循环中支持 break/continue 语义
- 保持语义正确且不引入新的 IR 结构

计划：
- 在 StmtLowerer 的循环展开中检测 break/continue 语句
- 仅当 break/continue 的条件可用 EvalContext 静态求值时生效
- break: 终止展开；continue: 跳过本次迭代余下语句
- 不可静态求值则维持现有回退策略并发出诊断
- 增补 break/continue 静态求值用例与断言
- 更新 workflow/architecture 对可支持范围的说明

实施：
- StmtLowerer 的 repeat/for/foreach 静态展开支持 break/continue，并在 guard 可静态求值时执行
- 若循环体含 break/continue 且 guard 不可静态求值 -> 直接报错不支持
- 新增 break/continue 相关 fixture 与 `convert-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0022 - Pass5 动态 break/continue guard 传播支持

目标：
- 对“综合可接受”的 break/continue 提供语义等价的 guard 展开
- 覆盖 if/else、case 中的 break/continue，并保持展开后写回顺序一致

计划：
- 在循环展开期间引入 per-iteration 的 flow guard（alive/skip）
- break 生成“终止后续迭代”的 guard 更新；continue 生成“跳过当前迭代后续语句”的 guard
- 在语句序列中向下传播 guard，支持嵌套 if/else/case 中的 break/continue
- 明确 guard 合并规则与不可支持场景的诊断策略
- 增补动态 break/continue 的测试与 fixture
- 更新 workflow/architecture 对 guard 传播流程的描述

实施：
- StmtLowerer 为 repeat/for/foreach 引入动态 break/continue 的 flow guard 传播（loopAlive/flowGuard）
- break 更新 `loopAlive`，continue 更新 `flowGuard`，保证后续语句与迭代被正确屏蔽
- 新增动态 break/continue 的 fixture 与 `convert-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0023 - Pass5 测试覆盖扩展

目标：
- 覆盖 case inside、pattern 条件报错、while/do/forever 报错、展开上限、位选/范围选 LHS

计划：
- 扩展 `tests/data/convert/stmt_lowerer.sv` 增加新模块用例
- 在 `tests/convert/test_convert_stmt_lowerer.cpp` 增加断言与诊断验证
- 需要时新增独立测试目标以区分错误路径与正常路径

实施：
- `tests/data/convert/stmt_lowerer.sv` 增补 case inside、展开上限与 LHS 选择用例
- `tests/convert/test_convert_stmt_lowerer.cpp` 增加 case inside/展开上限/LHS 选择断言
- `tests/data/convert/stmt_lowerer_errors.sv` 增补 pattern/while/do/forever 错误用例
- `tests/convert/test_convert_stmt_lowerer_errors.cpp` 覆盖 pattern/while/do/forever 错误路径断言

完成情况：已完成

## STEP 0024 - Pass5 LHS 复合目标与成员选择支持

目标：
- 支持 assignment/continuous assign 的 LHS 复合目标（concatenation/streaming concat）
- 支持 struct/array member select 作为 LHS，避免仅支持命名值/位选/范围选
- 将复合 LHS 拆解为多条 WriteIntent，保持赋值顺序与 guard 语义一致

计划：
- 扩展 `WriteIntent` 表达能力（多 target 或拆分策略），补充 LHS 语义解析
- 在 StmtLowerer 解析 LHS 时识别 concat/streaming 与 member select
- 新增 LHS 复合目标/成员选择 fixture 与断言
- 更新 workflow/architecture 对 LHS 支持范围的说明

实施：
- 扩展 `WriteSliceKind` 支持 MemberSelect 并在 StmtLowerer 解析 struct/array member LHS
- StmtLowerer 支持 concat/streaming concat LHS，拆分 RHS 为多条 WriteIntent
- 新增 `stmt_lowerer_lhs_concat/stream/member` fixture 与 `convert-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0025 - Pass5 TimedStatement 语义处理与诊断

目标：
- 明确处理 `#delay` / `@event` / `wait` 等 timed 语句，避免当前“忽略 timing”的隐式语义偏差
- 对不可支持的 timing 控制给出 error 诊断
- 关注点仅限可综合语法，不可综合语法留作未来支持

计划：
- 在 StmtLowerer 识别 TimedStatement / wait / wait fork / wait order / event trigger / disable fork
- 对 timing/并发控制发出 warning，忽略语义并继续降级可执行 body
- 增补 timed 语句 fixture 与 warning 断言
- 更新 workflow/architecture 对 timing 处理策略的说明

实施：
- StmtLowerer 遇到 TimedStatement / wait / wait fork / wait order / event trigger / disable fork
  发出 warning，忽略 timing/并发语义并继续降低可执行 body
- 新增 timed delay/event/wait/wait fork/disable fork/event trigger 的 fixture 与断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0026 - Pass5 while/do-while/forever 静态展开支持

目标：
- 支持可静态求值的 while/do-while/forever 展开（受 maxLoopIterations 限制）
- 对不可静态求值的循环保持报错并终止降级
- 保持 break/continue guard 语义与现有循环展开一致
- 关注点仅限可综合语法，不可综合语法留作未来支持

计划：
- 复用 For/Repeat 的静态求值与 loop guard 逻辑，新增 while/do-while/forever 入口
- 维持 `maxLoopIterations` 上限与诊断信息一致性
- 新增 while/do-while 静态展开 fixture 与断言
- 更新 workflow/architecture 文档；标注此步骤将取代 STEP 0018 中“while/do/forever 报错”的行为

实施：
- StmtLowerer 新增 while/do-while/forever 静态展开路径与 EvalContext 评估逻辑
- 循环展开统一先 dry-run，失败直接报错并不产出写回
- 新增 while/do-while/forever 静态展开 fixture 与 `convert-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0027 - Pass5 调试系统任务与 DPI 调用降级

目标：
- 在 Pass5 识别并保留调试系统任务（例如 `$display/$info/$warning/$error/$fatal`）
- 支持 DPI-C 导入函数的语句调用（仅 input/output 方向），并支持可选返回值
- 保留语句顺序，emit 侧按 GRH 的调用/副作用 OP 规范化输出
- 仅在 edge-sensitive 触发列表内生成 display/DPI 调用；否则丢弃并诊断
- 对不可识别的调用给出明确诊断，保持可综合路径不受影响

计划：
- 扩展 `LoweringPlan`，新增统一 `LoweredStmt` 列表（Write/Display/Assert/DpiCall），
  记录 kind/op、updateCond、location、eventEdge、event operands、以及参数列表
- 引入事件绑定：在进入过程块时解析 edge-sensitive timing control，生成 event operands + eventEdge；
  若无法解析或非 edge-sensitive，display/DPI 调用按 GRH 要求丢弃并诊断
- 参数建模：`in` -> `ExprNodeId`；`out` -> 结果 value；
  可选返回值通过 `hasReturn` 与 `results[0]` 表示
- StmtLowerer 在 `ExpressionStatement` 中识别系统任务与 DPI 调用，解析方向并生成 intent
- 复用 Pass4 表达式降级生成实参值（仅语句级调用，不扩展 Pass4 表达式调用）
- 新增 `stmt_lowerer` fixture 与断言覆盖 `$display` 与 DPI 调用顺序与返回值占位
- 更新 workflow/architecture，补充 Pass5 处理“副作用语句”的流程与限制

实施：
- `LoweringPlan` 新增 `LoweredStmt` 列表与 Display/Assert/DpiCall 数据结构
- StmtLowerer 进入过程块时解析 edge-sensitive timing control，填充 event operands/edges
- `ExpressionStatement` 支持 `$display/$write/$strobe` 与 `$info/$warning/$error/$fatal` 降级
- DPI-C import function 语句调用支持 input/output 方向与可选返回值占位
- 非 edge-sensitive 事件调用丢弃并给出 warning 诊断
- 新增 `stmt_lowerer_display/dpi` fixture 与 `convert-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0028 - Pass6 状态写回合并与 kRegister/kLatch 建模

目标：
- 将顺序/锁存类写回统一为 `updateCond + nextValue` 形式，适配新的 kRegister/kLatch
- 结合事件绑定（event operands + eventEdge）生成边沿触发更新语义
- 保留语句顺序与 guard 优先级，生成稳定的 mux/nextValue 表达式

计划：
- 引入/扩展写回合并 pass（Pass6/Pass7），按 `target + domain + event` 聚合 Write 类语句
- 基于 `LoweredStmt` 顺序合并 guard：`updateCond = OR(guards)`，
  `nextValue = mux(guard_n, value_n, ..., oldValue)`（按原语句顺序）
- 顺序 domain 使用 event operands + eventEdge；缺失 edge-sensitive 事件时诊断并跳过
- 锁存 domain 生成 kLatch（无事件列表），仅使用 updateCond/nextValue
- 更新 workflow/architecture，补充写回合并与 nextValue 构建流程
- 新增回归用例覆盖 reset/enable 优先级与顺序稳定性

实施：
- `WriteBackPlan` 扩展为 `Entry{target, signal, domain, updateCond, nextValue, eventEdges, eventOperands}` 结构
- StmtLowerer 写回语句补齐 `updateCond` 与事件绑定信息
- 新增 Pass6 WriteBackPass：按 `target + domain + event` 聚合写回，生成 `updateCond/nextValue`
- 顺序域缺失 edge-sensitive 事件时给出 warning 并跳过写回
- 写回合并暂不支持 LHS 切片，遇到切片写回记录 TODO
- 新增 `tests/data/convert/write_back.sv` 与 `convert-write-back` 用例，覆盖顺序/锁存与缺失事件
- 更新 workflow/architecture 文档对齐 Pass6 写回合并流程

完成情况：已完成

## STEP 0029 - Pass7 MemoryPortLowerer 适配新 kMemoryReadPort/kMemoryWritePort

目标：
- 使用 `kMemoryReadPort` 表达组合读，使用 `kMemoryWritePort` + `updateCond`/mask 表达写入
- 同步读通过 `kRegister` 捕获 `kMemoryReadPort` 输出建模
- 复用 event operands + eventEdge 绑定时序端口

计划：
- 扩展 MemoryPortLowerer：为读端口生成 `kMemoryReadPort`，必要时插入 `kRegister`
- 写端口生成 `kMemoryWritePort`，mask 为空时补常量全 1
- 解析时序域的事件列表，缺失 edge-sensitive 事件则诊断并跳过
- 更新 workflow/architecture 文档与示例
- 增加 memory 读/写/掩码/同步读用例与断言

实施：
- `LoweringPlan` 新增 `MemoryReadPort/MemoryWritePort` 结构与存储列表
- Pass7 MemoryPortLowerer：从 `loweredStmts` 中识别 `mem[addr]` 读写访问
- 读端口：顺序域要求 edge-sensitive 事件绑定，缺失则 warning 丢弃
- 写端口：支持全写与 bit/range 掩码写，生成 `updateCond/data/mask`
- 新增 `memory_ports.sv` fixture 与 `convert-memory-port-lowerer` 断言
- 更新 workflow/architecture 文档对齐 MemoryPortLowerer 输出

完成情况：已完成

## STEP 0030 - Pass7 MemoryPortLowerer 掩码写能力补强

目标：
- 支持 dynamic range 的掩码写（`[base +: width]` / `[base -: width]`）
- 支持多维 unpacked memory 的地址线性化与端口降级
- 对同步读补充 enable/reset 的建模入口（与 kRegister 结合）

计划：
- 扩展 MemoryPortLowerer：为 IndexedUp/IndexedDown 生成可组合 mask 与 data shift
- 增加多维 memory 地址线性化策略（结合 `unpackedDims` 计算 row index）
- 引入 read-enable/reset 识别规则，生成寄存器捕获输入
- 更新 workflow/architecture 文档与示例
- 新增覆盖 dynamic range、multi-dim memory、sync read enable/reset 的用例与断言

实施：
- MemoryPortLowerer 支持 IndexedUp/IndexedDown 动态掩码写，生成 mask/data shift 表达式
- 多维 unpacked memory 支持地址线性化，按 `idx*stride` 聚合
- 顺序读端口记录 `updateCond` 作为 enable 建模入口
- 新增 `memory_ports.sv` 动态范围与多维写用例，扩展 `convert-memory-port-lowerer` 断言
- 更新 workflow/architecture 文档对齐 MemoryPortLowerer 输出细节

完成情况：已完成

## STEP 0031 - Pass7 MemoryPortLowerer 语义约束与索引修正

目标：
- Indexed part-select 的 `width` 必须为常量表达式，非法时给出诊断
- 动态范围写入做越界检测（base/width 超出 memory 宽度时给出诊断或 skip）
- 多维 unpacked memory 线性化时考虑下界与方向（如 `[7:4]` / 降序范围）

计划：
- 扩展 MemoryPortLowerer：为 `[base +: width]` / `[base -: width]` 验证 `width` 常量
- 引入 bounds 检查：对可静态求值的 base/width 做超界诊断；动态情况下生成 warning/skip 策略
- 使用 `unpackedDims` 与 range metadata 修正线性化：加入 lower-bound 偏移与反向索引处理
- 更新 workflow/architecture 文档与示例
- 新增覆盖非法 width、越界、降序范围/非 0 起始下界的用例与断言

实施：
- `SignalInfo.unpackedDims` 引入 `UnpackedDimInfo{extent,left,right}`，TypeResolver 记录范围上下界
- MemoryPortLowerer 线性化前按范围方向修正索引（升序 `idx-left`，降序 `left-idx`）
- Indexed part-select width/越界检查加入 warning 诊断，非法写入直接跳过
- 新增 `memory_ports.sv` 下界/降序、非法 width、越界动态范围用例
- 扩展 `convert-memory-port-lowerer` 断言覆盖索引修正与诊断
- 更新 workflow/architecture 文档对齐新约束

完成情况：已完成

## STEP 0032 - Pass7 MemoryPortLowerer 常量表达式求值与 bounds 统一

目标：
- Indexed part-select 的 `width` 支持参数/简单表达式常量求值
- Simple range 与 Indexed range 的越界判断覆盖更多可静态求值场景
- 无法静态判断时采用统一 warning + skip 策略

计划：
- 扩展 `evalConstInt`：支持参数符号与简单常量表达式折叠
- 统一 bounds 检查逻辑：对可静态求值的 base/width/left/right 做越界诊断
- 新增覆盖参数 width、表达式 width、简单范围越界的用例与断言
- 更新 workflow/architecture 文档说明 const-eval 规则

实施：
- `evalConstInt` 支持参数符号 + 简单表达式常量折叠
- Indexed part-select 宽度/越界检查覆盖参数与表达式常量
- 新增 param/expr width 与 simple range 越界用例断言
- 更新 workflow/architecture 文档说明 const-eval 范围

完成情况：已完成

## STEP 0033 - Pass7 MemoryPortLowerer 常量范围扩展与动态 bounds 提示

目标：
- 常量求值支持 package param / localparam / 更丰富的算术与比较表达式
- 动态 base/width 提示更明确，便于定位无法静态 bounds 的写入
- 补充 package param/动态 base 的用例与断言

计划：
- 扩展 `evalConstInt`：覆盖 package 参数与逻辑/比较运算折叠
- Indexed range：base 为动态表达式时发出 warning 并继续生成
- 新增 package width 与 dynamic base warning 用例
- 更新 workflow/architecture 文档说明 const-eval 范围

实施：
- `evalConstInt` 支持包参数解析与逻辑/比较常量折叠
- Indexed range base 动态时提示 warning 并继续生成
- 新增 package width 与 dynamic base warning 用例
- 更新 workflow/architecture 文档说明 const-eval 范围

完成情况：已完成

## STEP 0034 - Pass7 MemoryPortLowerer 显式包参数与复杂表达式求值

目标：
- 常量求值支持 `pkg::PARAM` 显式限定名
- 扩展常量折叠覆盖更复杂表达式（如条件运算、拼接/复制与其内部算术）
- 新增 explicit pkg::PARAM 与复杂表达式用例

计划：
- ExprLowerer 识别参数符号并直接下沉为常量节点
- `evalConstInt` 支持 `kMux`（三目）与 `kConcat/kReplicate` 常量折叠
- concat/replicate 允许嵌套算术表达式，width 推断依赖表达式宽度提示
- 新增 `mem_write_dynamic_pkg_qualified` 与 `mem_write_dynamic_expr_complex` 用例
- 新增 concat/replicate 宽度用例断言（含嵌套算术）
- 更新 workflow/architecture 文档说明显式包参数与复杂常量折叠

实施：
- ExprLowerer 将参数符号降级为常量节点，覆盖显式 `pkg::PARAM`
- ExprNode 增加 `widthHint`，在 ExprLowerer 中记录表达式宽度
- `evalConstInt` 支持 `kMux`/`kConcat`/`kReplicate` 常量折叠与嵌套算术
- 新增 explicit pkg::PARAM、复杂表达式、concat/replicate 宽度用例断言
- 更新 workflow/architecture 文档说明 const-eval 范围

完成情况：已完成
