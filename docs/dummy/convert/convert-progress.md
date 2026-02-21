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
- 新增 `lib/include/ingest.hpp` 与 `lib/src/ingest.cpp` 的骨架定义
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
- `lib/include/ingest.hpp`/`lib/src/ingest.cpp` 更新 PlanSymbolTable 与索引化结构骨架
- 进一步明确并行化方法（任务单元、调度模型、去重与阶段边界）
- 新增 PlanCache/PlanTaskQueue 并接入 ConvertContext 骨架

完成情况：已完成

## STEP 0006 - 建立 ConvertContext 与 Plan 数据结构

目标：
- 明确 ConvertContext 与 Plan 相关的核心数据结构
- 为后续 Pass 实现提供最小可用骨架

实施：
- `lib/include/ingest.hpp` 新增 LoweringPlan/WriteBackPlan/PlanArtifacts 骨架
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
- `lib/src/ingest.cpp` 新增 TypeResolverPass 与类型解析辅助逻辑
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
- `lib/src/ingest.cpp` 新增 RWAnalyzerPass 与 AST 遍历/控制域分类逻辑
- ConvertDriver 在 Pass2 后调用 RWAnalyzerPass 更新 ModulePlan
- 新增 `convert-rw-analyzer` HDLBits 测试与 CMake 注册
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0011 - Pass4 ExprLowerer 落地

目标：
- 实现 Pass4（ExprLowererPass），将 RHS 表达式降级为 LoweringPlan
- 接入 Convert 主流程并补充测试

实施：
- `lib/include/ingest.hpp` 增补 LoweringPlan 节点结构与 ExprLowererPass 声明
- `lib/src/ingest.cpp` 实现 ExprLowererPass，覆盖常见 unary/binary/conditional/concat/replicate/select
- ~~ConvertDriver 在 Pass3 后生成并缓存 `loweringPlan`~~
- ConvertDriver 在 Pass2 后生成并缓存 `loweringPlan`
- 新增 `convert-expr-lowerer` 测试与 fixture
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0012 - Pass5 StmtLowerer 落地

目标：
- 实现 Pass5（StmtLowererPass），降级语句与控制流并生成写回意图
- 扩展 LoweringPlan 以承载 guard 与写回意图
- 增补测试覆盖 if/else 与连续赋值场景

实施：
- `lib/include/ingest.hpp` 新增 WriteIntent 与 `LoweringPlan.writes`
- `lib/src/ingest.cpp` 实现 StmtLowererPass，支持 if/else guard 叠加与写回意图生成
- ConvertDriver 接入 Pass5
- 新增 `ingest-stmt-lowerer` 测试与 fixture
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
- 新增 `ingest-stmt-lowerer` 对应用例与断言
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
- 不可静态求值的循环给出报错诊断并保持安全回退
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
- 将相关分支的诊断升级为 error
- ~~对 pattern 条件/PatternCase/while/do/forever 采用 “报错 + 跳过” 策略~~（已被 STEP 0026 替代）
- 更新 workflow/architecture 说明不支持范围
- 增补错误路径测试

实施：
- StmtLowerer 将 pattern condition 与 pattern case 升级为 error 并直接跳过分支
- ~~while/do-while/forever 直接报错并停止对 body 的降级~~（已被 STEP 0026 替代）
- 新增 `ingest-stmt-lowerer-errors` 测试与 fixture 覆盖错误路径
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
- 新增 break/continue 相关 fixture 与 `ingest-stmt-lowerer` 断言
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
- 新增动态 break/continue 的 fixture 与 `ingest-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0023 - Pass5 测试覆盖扩展

目标：
- 覆盖 case inside、pattern 条件报错、while/do/forever 报错、展开上限、位选/范围选 LHS

计划：
- 扩展 `tests/ingest/data/stmt_lowerer.sv` 增加新模块用例
- 在 `tests/ingest/test_ingest_stmt_lowerer.cpp` 增加断言与诊断验证
- 需要时新增独立测试目标以区分错误路径与正常路径

实施：
- `tests/ingest/data/stmt_lowerer.sv` 增补 case inside、展开上限与 LHS 选择用例
- `tests/ingest/test_ingest_stmt_lowerer.cpp` 增加 case inside/展开上限/LHS 选择断言
- `tests/ingest/data/stmt_lowerer_errors.sv` 增补 pattern/while/do/forever 错误用例
- `tests/ingest/test_ingest_stmt_lowerer_errors.cpp` 覆盖 pattern/while/do/forever 错误路径断言

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
- 新增 `stmt_lowerer_lhs_concat/stream/member` fixture 与 `ingest-stmt-lowerer` 断言
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
- 新增 while/do-while/forever 静态展开 fixture 与 `ingest-stmt-lowerer` 断言
- `docs/convert/convert-workflow.md` 与 `docs/convert/convert-architecture.md` 同步更新

完成情况：已完成

## STEP 0027 - Pass5 调试系统任务与 DPI 调用降级

目标：
- 在 Pass5 识别并保留调试系统任务（例如 `$display/$info/$warning/$error/$fatal`）
- 支持 DPI-C 导入函数的语句调用（仅 input/output 方向），并支持可选返回值
- 保留语句顺序，emit 侧按 GRH 的调用/副作用 OP 规范化输出
- 优先使用 edge-sensitive 触发列表生成 system task/DPI 调用；不满足上下文时诊断并丢弃
- 对不可识别的调用给出明确诊断，保持可综合路径不受影响

计划：
- 扩展 `LoweringPlan`，新增统一 `LoweredStmt` 列表（Write/SystemTask/DpiCall），
  记录 kind/op、updateCond、location、eventEdge、event operands、以及参数列表
- 引入事件绑定：在进入过程块时解析 edge-sensitive timing control，生成 event operands + eventEdge；
  若无法解析，system task/DPI 调用按上下文规则决定是否保留并诊断
- 参数建模：`in` -> `ExprNodeId`；`out` -> 结果 value；
  可选返回值通过 `hasReturn` 与 `results[0]` 表示
- StmtLowerer 在 `ExpressionStatement` 中识别系统任务与 DPI 调用，解析方向并生成 intent
- 复用 Pass5 表达式降级生成实参值（仅语句级调用，不扩展独立表达式调用）
- 新增 `stmt_lowerer` fixture 与断言覆盖 `$display` 与 DPI 调用顺序与返回值占位
- 更新 workflow/architecture，补充 Pass5 处理“副作用语句”的流程与限制

实施：
- `LoweringPlan` 新增 `LoweredStmt` 列表与 SystemTask/DpiCall 数据结构
- StmtLowerer 进入过程块时解析 edge-sensitive timing control，填充 event operands/edges
- `ExpressionStatement` 支持 `$display/$write/$strobe` 与 `$info/$warning/$error/$fatal` 降级
- DPI-C import function 语句调用支持 input/output 方向与可选返回值占位
- 非支持的过程块上下文给出 warning 诊断
- 新增 `stmt_lowerer_display/dpi` fixture 与 `ingest-stmt-lowerer` 断言
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
- 写回合并暂不支持 LHS 切片，遇到切片写回记录报错
- 新增 `tests/ingest/data/write_back.sv` 与 `ingest-write-back` 用例，覆盖顺序/锁存与缺失事件
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
- 新增 `memory_ports.sv` fixture 与 `ingest-memory-port-lowerer` 断言
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
- 新增 `memory_ports.sv` 动态范围与多维写用例，扩展 `ingest-memory-port-lowerer` 断言
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
- 扩展 `ingest-memory-port-lowerer` 断言覆盖索引修正与诊断
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

## STEP 0035 - Pass8 GraphAssembly 基础落地

目标：
- 将 ModulePlan + LoweringPlan + WriteBackPlan 落地为 GRH Graph
- 完成端口/信号/常量/组合逻辑的基础发射
- ConvertDriver 接入 Pass8 并填充 Netlist/topGraphs

计划：
- GraphAssembler 建立 Graph 与符号表映射（PlanSymbolId -> ValueId/OperationId）
- 端口 Value 建模：input/output/inout（__in/__out/__oe）并绑定 Graph ports
- ExprNode 发射：kConstant/kAssign/逻辑运算/kMux/kSlice/kConcat/kReplicate 等
- WriteBackPlan 发射：comb -> kAssign，seq -> kRegister，latch -> kLatch
- ConvertDriver 在 Pass7 后调用 GraphAssembler，写入 Netlist 并标记顶层 Graph
- 新增 `ingest-graph-assembly-basic` 测试覆盖组合/时序/锁存基本路径
- 更新 workflow/architecture 文档对齐 Pass8 基础流程

实施：
- GraphAssembler 落地 GraphAssemblyState，完成端口/信号 Value 与 ExprNode 发射
- WriteBackPlan 落地为 kAssign/kRegister/kLatch，并生成 eventEdge 属性
- ConvertDriver 接入 GraphAssembler 并标记顶层 graph
- 新增 `ingest-graph-assembly-basic` 测试与 `graph_assembly_basic.sv` fixture
- 更新 workflow/architecture 文档对齐 Pass8 基础流程

完成情况：已完成

## STEP 0036 - Pass8 Memory 端口与 kMemory 落地

目标：
- 生成 kMemory/kMemoryReadPort/kMemoryWritePort 并串联同步读寄存器
- 对接 Pass7 生成的 memoryReads/memoryWrites

计划：
- 为 memory 信号创建 kMemory op，填充 width/row/isSigned
- memoryReads：kMemoryReadPort + (isSync ? kRegister : 直接返回)
- memoryWrites：kMemoryWritePort，填充 updateCond/addr/data/mask/eventEdge
- 缺失 edge-sensitive 事件时保持诊断与跳过策略
- 新增 `ingest-graph-assembly-memory` 测试覆盖 async/sync read 与 mask write
- 更新 workflow/architecture 文档对齐 memory 组装策略

实施：
- GraphAssembly 支持 kMemory/kMemoryReadPort/kMemoryWritePort 与同步读寄存器
- memoryReads/memoryWrites 依据 memSymbol 与 eventEdge 完成属性填充与诊断
- 新增 `ingest-graph-assembly-memory` 测试与 `graph_assembly_memory.sv` fixture
- 更新 workflow/architecture 文档对齐 memory 组装策略

完成情况：已完成

## STEP 0037 - Pass8 副作用语句与 DPI 支持

目标：
- 落地 SystemTask/DpiCall 为 GRH op
- 生成 DPI import（kDpicImport）并保障 call 解析规则

计划：
- LoweredStmt(SystemTask/DpiCall) 发射为 kSystemTask/kDpicCall
- 建立 DPI import 扫描与 kDpicImport 生成（argsName/argsWidth/argsSigned 等）
- kDpicCall 写入 targetImportSymbol/inArgName/outArgName/hasReturn/eventEdge
- 新增 `ingest-graph-assembly-dpi-display` 测试覆盖 system task/dpi 调用链路
- 更新 workflow/architecture 文档对齐副作用语句与 DPI 约束

实施：
- StmtLowerer 记录 `LoweringPlan.dpiImports` 并校验 DPI import 签名一致性
- GraphAssembly 发射 kSystemTask/kDpicCall 与 kDpicImport，补齐 eventEdge/返回值元数据
- Emit 支持 eventEdge 驱动 system task/dpi 调用，并解析 argsSigned/return 信息
- 新增 `ingest-graph-assembly-dpi-display` 测试与 `graph_assembly_dpi_display.sv` fixture
- 更新 workflow/architecture 文档对齐 DPI/副作用语句组装规则

完成情况：已完成

## STEP 0038 - Pass8 实例化与黑盒参数落地

目标：
- 生成 kInstance/kBlackbox，完整保留层次与黑盒参数
- 完成图名/alias 注册与实例连接规则

计划：
- 基于 PlanKey -> GraphName 建立映射，实例化时解析子模块目标图
- kInstance：写入 moduleName/portName 列表，构建 operands/results 顺序
- kBlackbox：附带 parameterNames/parameterValues
- inout 端口采用 __in/__out/__oe 连接；不支持形态给出诊断并跳过
- 顶层实例写入 topGraphs 与 alias（instance/definition 名）
- 新增 `ingest-graph-assembly-instance` 测试覆盖普通实例与黑盒参数
- 更新 workflow/architecture 文档对齐实例化规则

实施：
- `InstanceInfo` 记录 `InstanceSymbol` 与 `paramSignature`，保留参数特化信息与黑盒参数
- `GraphAssembler` 增加 `PlanKey -> GraphName` 映射（含参数 hash），避免图名冲突
- GraphAssembly 新增 `emitInstances`：生成 `kInstance/kBlackbox`、写入 moduleName/portName/instanceName
- inout 连接使用 `__out/__oe` 操作数与 `__in` 结果；输出端口仅允许简单 symbol
- 黑盒实例补齐 `parameterNames/parameterValues`，普通实例使用 GraphName 映射
- 顶层实例注册 alias（instance/definition 名）并保留 topGraphs
- 新增 `graph_assembly_instance.sv` fixture 与 `ingest-graph-assembly-instance` 测试
- 更新 workflow/architecture 文档对齐实例化规则

完成情况：已完成

## STEP 0039 - LHS 切片写回的 WriteBack/GraphAssembly 支持

目标：
- 支持 bit/range/member select 的部分写回语义
- 打通 WriteBack 与 GraphAssembly 对切片写回的完整链路

计划：
- WriteBackPass：允许 slices，生成目标值的 read/modify/write nextValue
- GraphAssembly：使用 kSliceStatic/kSliceDynamic/kConcat 组装更新值
- 多 slice 合并与 guard 优先级按语句顺序处理
- 新增 `ingest-write-back-slice` 与 `ingest-graph-assembly-slice` 测试
- 更新 workflow/architecture 文档对齐切片写回流程

实施：待开始

实施：
- WriteBackPass 支持带 slices 的写回合并，按语句顺序进行 read/modify/write
- 静态 bit/range/member select 使用 kSliceDynamic + kConcat 生成 nextValue
- 动态 bit/part-select 使用 mask + shift 生成 nextValue，并保留诊断
- 新增 `ingest-write-back-slice` 与 `write_back_slice.sv` fixture 覆盖静态/动态/member
- 新增 `ingest-graph-assembly-slice` 与 `graph_assembly_slice.sv` fixture 覆盖图组装
- 更新 workflow/architecture 文档对齐切片写回流程

完成情况：已完成

## STEP 0040 - CLI 主流程接入 Convert/Emit（HDLBits 预备）

目标：
- 在 `wolf-sv-parser` 主流程中接入 Convert + Transform + Emit
- 支持 HDLBits 流程所需的 `--emit-sv/--emit-json/-o/--emit-out-dir` 参数

计划：
- 主流程创建 `ConvertDriver`，将 Convert 诊断/日志输出到 stderr
- Convert 输出 Netlist 后运行 transform passes，再调用 JSON/SV emit
- `-o` 同步输出目录与文件名规则，兼容 HDLBits `make run_hdlbits_test`
- 更新 workflow/architecture 文档说明 CLI 接入与输出规则

实施：
- `src/main.cpp` 接入 `ConvertDriver`，按 CLI 参数配置日志与错误处理
- Transform 阶段保持默认 passes（const-fold + stats），输出 Netlist
- Emit 阶段支持 JSON/SV 输出与 `-o/--emit-out-dir` 目录重定向
- 更新 workflow/architecture 文档补充 CLI 入口与 HDLBits 输出说明

完成情况：已完成

## STEP 0041 - HDLBits 输出简化与无条件写回修复

目标：
- 修复 comb 写回生成自引用 MUX（`cond ? new : old`）导致 HDLBits 001 多余 `__const_*`/`_expr_tmp_*`
- 常量与表达式临时值尽量折叠，输出更接近原始语义（如 `assign one = 1'b1`）

计划：
- 追踪 `WriteBackPass` 构建 `nextValue` 的逻辑，确认无条件写入被 `baseValue` 兜底触发自引用 MUX
- 在 WriteBack 阶段识别“单条无条件写”与“guard 恒真”场景：直接返回 `writeValue`，避免 `mux(guard, writeValue, baseValue)`
- 对 comb 多写合并的 MUX 链避免引入 `baseValue` 自引用，必要时显式用 `X`/默认值替代
~~- 在 GraphAssembly 或 const-fold 中折叠常量 MUX，并对相同常量值做缓存复用~~
- 补充 HDLBits 001 相关测试/fixture（convert/emit），断言输出不含自引用 MUX 与重复常量
- 若规则变化影响说明，更新 workflow/architecture 文档补充 comb 写回简化策略

实施：
- WriteBackPass 针对无切片写回：识别 guard 恒真/缺失，避免默认 `baseValue` 兜底 MUX；组合域缺少无条件写回时提升为 latch
- 保留切片写回的 read/modify/write 基底（`oldValue`）以覆盖部分位
- 识别 `cond || !cond` 等恒真 guard 且 RHS 非自引用时保持 comb，避免 HDLBits 031 生成多余 `always`
- 新增 `write_back_comb` fixture 与测试，更新 latch 场景期望为直接 `nextValue`
- 更新 workflow/architecture 文档说明写回简化与 comb->latch 规则

完成情况：已完成



## STEP 0042 - 逻辑操作数与切片拼接简化

目标：
- 逻辑运算（kLogicAnd/kLogicOr）输出仍对多位信号进行 reduction OR 归一
- 简化 `concat -> slice` 的冗余链路，避免在 emit 里展开无意义的拼接/切片

实施：
- `src/emit.cpp` 恢复 `logicalOperand` 对多位信号包 `(|value)` 的输出形式
- `lib/transform/const_fold.cpp` 新增 `kSliceStatic` + `kConcat` 的窥孔简化：当切片范围完全覆盖某个 concat 片段时直接替换为该片段

完成情况：已完成



## STEP 0043 - 写回切片全覆盖的直接拼接

目标：
- 在切片写回覆盖完整位宽且无重叠时，直接构造 concat，避免链式 slice/concat
- 清理 HDLBits 017 输出中的冗余中间信号

实施：
- `lib/src/ingest.cpp` 在 WriteBack 生成阶段识别全覆盖静态切片，直接拼接 RHS 片段生成 `nextValue`
- 保持非覆盖/有重叠/动态切片路径不变

完成情况：已完成



## STEP 0044 - Case 二值优先与四值回退（综合友好）

目标：
- 对 case 语句优先按二值逻辑分析，若分支覆盖完整或存在 default，则输出纯组合逻辑
- 若二值逻辑不完整，则回退到四值逻辑以保证语义完整，并提示可能不可综合

计划：
- 在 case 降低阶段引入“二值覆盖分析”，判断是否在二值语义下完整覆盖或含 default
- 对二值覆盖完整的 case 生成组合逻辑（== 比较、无 latch），避免 === 与 always_latch
- 对二值不完整的 case 保持四值语义（=== 与 latch），并输出警告提示“生成四值语义，可能不可综合”
- 在诊断中标注源码位置，建议用户补齐 default 或完整覆盖以回到可综合输出

实施：
- `StmtLowererState` 新增二值覆盖分析与 case 降低模式（TwoState/FourState），默认优先二值匹配
- 无 default 且二值覆盖不完整时回退 `kCaseEq`/mask+`kCaseEq`，并输出“4-state semantics”警告
- 写回 `WriteIntent` 增加 `coversAllTwoState` 标记，WriteBack 在组合域判断无 latch 时纳入该标记
- 新增/更新 convert fixtures 与测试：case 完整覆盖走 `kEq`，不完整 case 触发 warning
- 修复 case 控制表达式宽度判定：剥离隐式转换，优先使用原始信号位宽，避免无尺寸常量抬宽导致误判不完整覆盖

完成情况：已完成

## STEP 0045 - Transform pass 合并与冗余清理

目标：
- 合并 `const_inline`/`output_assign_inline`/`redundant_elim` 为单次遍历的 peephole pass
- 清理旧 pass 与测试目标，减少编译与遍历开销

实施：
- `RedundantElimPass` 承接常量输出内联、输出端口 assign 内联、单输入 concat/assign 链与 NOT/XOR 折叠
- 移除 `const_inline`/`output_assign_inline` 的源码、头文件与测试目标
- Transform pipeline 仅保留 `const_fold -> redundant_elim -> dead_code_elim -> stats`

测试：
- HDLBits 全量测试（用户执行）

完成情况：已完成



## STEP 0046 - 移除 RWAnalyzer Pass3

目标：
- 移除 Pass3（RWAnalyzer）相关实现与调用，避免维护未使用的分析结果
- 清理配套测试与文档描述，保证架构/流程一致

计划：
- 删除 `RWAnalyzerPass`、`RWAnalyzerState`、`RWVisitor` 与相关辅助函数（`encodeRWKey`/`encodeMemKey` 等）
- 移除 `ModulePlan.rwOps`/`memPorts` 与 `RWOp`/`MemoryPortInfo` 数据结构（若无其他依赖）
- 移除 `tests/ingest/test_ingest_rw_analyzer.cpp` 并更新 `CMakeLists.txt` 测试目标
- 更新文档：`docs/convert/convert-architecture.md`、`docs/convert/convert-workflow.md`、`docs/convert/convert-kimi.md` 中的 Pass3/RWOp/memPorts 描述

实施：
- 删除 `RWAnalyzerPass`、`RWAnalyzerState`、`RWVisitor` 与相关辅助函数
- `ModulePlan` 移除 `rwOps/memPorts` 与相关类型定义
- ConvertDriver 移除 Pass3 构建与调用链路
- 删除 `convert-rw-analyzer` 测试目标与源码
- 更新 convert 相关测试，移除对 Pass3 的依赖
- 同步更新 convert-architecture/workflow/kimi 文档
- 修复 casez/casex 通配匹配的常量掩码复用问题（避免 HDLBits 035 输出错误）

测试：
- `cmake --build build -j$(nproc)`
- `make run_hdlbits_test DUT=035`
- `make run_hdlbits_test`

完成情况：已完成



## STEP 0047 - 合并 Pass4/Pass5（ExprLowerer/StmtLowerer）

目标：
- 合并 ExprLowererPass 与 StmtLowererPass，减少重复的表达式降级逻辑
- 移除 `LoweringPlan.roots`/`nextRoot` 等跨 Pass 依赖，统一由单 Pass 生成 `LoweringPlan`

计划：
- 在 `lib/src/ingest.cpp` 里合并两套 `lowerExpression` 实现，保留 Pass5 版本并删除 Pass4 版本
- 移除 `LoweredRoot` 与 `LoweringPlan.roots`，以及 `ExprLowererState` 与 `takeNextRoot/resolveAssignmentRoot`
- 调整 `StmtLowererState::handleAssignment` 与 `scanExpression`，直接对 RHS 统一降级并缓存
- 更新 `ConvertDriver` 调度，删除 Pass4 实例与调用（保留 Pass5）
- 更新文档：`convert-workflow`/`convert-architecture`/`convert-kimi` 中关于 Pass4/roots/nextRoot 的描述
- 清理测试/fixtures（若有依赖 Pass4 行为的断言）

实施：
- 移除 `ExprLowererPass`/`ExprLowererState` 与 `LoweredRoot`/`LoweringPlan.roots` 相关实现与声明
- StmtLowerer 直接调用 `lowerExpression` 降级 RHS，删除 `takeNextRoot/resolveAssignmentRoot` 等跨 Pass 依赖
- StmtLowerer 负责初始化 `LoweringPlan`（values/tempSymbols/writes 等），ConvertDriver 仅执行 Pass5
- 清理 `convert-expr-lowerer` 测试与 fixture，更新其余测试仅依赖 StmtLowerer
- 同步更新 convert-architecture/workflow/kimi 文档对齐合并后的 Pass5

完成情况：已完成



## STEP 0048 - 合并 Pass1/Pass2（符号收集 + 类型解析）

目标：
- 将类型解析并入 Pass1，减少重复 AST 扫描
- 移除 `TypeResolverPass` 与相关调度，统一由 `ModulePlanner` 产出完整 ModulePlan
- 同步测试与文档，保持架构/流程一致

计划：
- 在 Pass1 端口/信号收集时直接填充宽度/维度/符号信息
- 删除 `TypeResolverPass` 声明与实现，移除 `ConvertDriver` 调度
- 更新 convert 测试（移除 TypeResolverPass 调用）
- 更新文档：`convert-workflow`/`convert-architecture`/`convert-kimi`

实施：
- `collectPorts/collectSignals` 直接调用类型解析逻辑并填充 `PortInfo/SignalInfo`
- 删除 `TypeResolverPass` 声明/实现与 `ConvertDriver` 调度
- 更新 convert 测试仅依赖 `ModulePlanner` 生成完整 ModulePlan
- 同步更新 convert-architecture/workflow/kimi 文档

测试：
- `cmake --build build -j$(nproc)`
- `ctest --test-dir build --output-on-failure`

完成情况：已完成

## STEP 0049 - Convert pass 耗时日志

目标：
- 在 Convert 各 pass 结束后输出耗时，便于定位卡顿或死循环
- 通过 ConvertLogger 的 level/tag 控制日志噪声

计划：
- 在 `ConvertDriver::convert` 中为 Pass1/Pass5/Pass6/Pass7/Pass8 添加计时
- 使用 `ConvertLogger` 输出 tag=timing 的 info 日志
- 同步更新 convert-architecture/workflow 文档

实施：
- 为 Pass1/Pass5/Pass6/Pass7/Pass8 添加耗时日志
- 增加 Convert 总耗时与 transform/emit 阶段耗时日志
- 日志仅在 `ConvertLogger` 启用且 level>=info 时输出（tag=timing）
- 更新 convert-architecture/workflow 文档说明 timing 日志

测试：
- 未运行（仅日志输出改动）

完成情况：已完成
~~备注：Pass3（RWAnalyzer）已在 STEP 0046 移除~~


## STEP 0050 - Convert 并行化架构与线程安全方案

目标：
- 以 Graph 创建为并行粒度，确保单个 Graph 串行创建
- 遇到新 instance 时创建新的 Graph 任务，确保 instance 不重复创建
- 明确诊断与 netlist 写回的线程安全策略，保证输出可复现

计划：
- 引入全局调度器（线程池）管理 Graph 任务队列，任务单元为 Graph 创建
- 线程池大小可参数化配置，默认 32
- Graph 创建过程保持串行；在 Graph 创建中发现新的 instance 时，计算 InstanceKey 并投递新的 Graph 任务
- 增加 `InstanceRegistry`：基于 `InstanceKey` 的互斥创建与 `future` 复用，避免重复实例
- Graph 任务调度按实例依赖推进（父 Graph 提交子 Graph 任务，可并发执行）
- 诊断采用线程本地收集 + 全局取消标志（fatal 时触发），最终统一排序输出
- netlist 写回采用 “Graph 结果本地化 + 单线程 commit”：写入 `NetlistSymbolTable` 分配 `GraphId` 并维护 `graphOrder`；顺序不要求稳定，只需确保符号可解析
- commit 阶段集中处理 `topGraphs` 与 `registerGraphAlias`，避免并发写入导致顺序不稳定或 alias 冲突
- instance 仅保留 `moduleName` 字符串属性；commit 时确保对应 graph 已注册，避免跨网表或未解析引用
- 明确线程安全共享缓存策略（只读 AST/符号表无锁，读多写少缓存用 shared_mutex）
- 新增 `--single-thread` 开关用于调试与回归对比
- 同步更新 `convert-architecture`/`convert-workflow` 文档与并行相关示例

实施：
- `ConvertOptions` 新增 `threadCount/singleThread`，CLI 增加 `--convert-threads/--single-thread`
- `PlanTaskQueue` 增加阻塞等待与 drain，配合并行 worker 收敛
- 引入 `InstanceRegistry` 去重与完成态登记，`enqueuePlanKey` 先判重再入队
- `ConvertDiagnostics` 支持线程本地缓冲与 flush；`ConvertLogger` 加锁避免并发交错
- `GraphAssembler` 图名解析加锁，`Netlist` 写回通过互斥串行化
- `ConvertDriver::convert` 使用线程池并行处理 Graph 任务，topGraphs/alias 在主线程统一注册
- 同步更新 `docs/convert/convert-architecture.md` 与 `docs/convert/convert-workflow.md`

完成情况：已完成

## STEP 0051 - System call 统一建模（kSystemFunction / kSystemTask）

目标：
- 统一 **system function（表达式）** 与 **system task（语句）** 的 IR 表达
- 可静态求解的 system function 继续折叠为常量；DPI 维持独立通路
- 为 `$random(seed)` / `$urandom_range` / 读写 mem / dump 波形等常见语义提供一致入口

计划（实现细化）：
1) **GRH/IR 定义**
   - 新增 `kSystemFunction`（表达式）与 `kSystemTask`（语句）op
   - `kSystemFunction` 属性：`name`、`hasSideEffects`（width/isSigned 在 Value 上）
   - `kSystemTask` 属性：`name`、`procKind/hasTiming`、`eventEdges/eventOperands`
   - 任务参数（含 format/file/exitCode）统一作为 operands，operands[0] 为 callCond
   - 保持 `kDpicCall` 独立（不并入 system call）
2) **Convert ExprLowerer（表达式侧）**
   - `$bits`：直接生成 `kConstant`
   - `$signed/$unsigned`：保持 `kAssign` cast
   - `$time/$stime/$realtime/$random/$urandom`：统一为 `kSystemFunction`
   - `$random(seed)` / `$urandom(seed)` / `$urandom_range`：先收敛到 `kSystemFunction`，标注 `hasSideEffects`
   - 其它未支持的 system function：保持报错（不做隐式降级）
3) **Convert StmtLowerer（语句侧）**
   - `$display/$write/$strobe/$fwrite/$fdisplay/$info/$warning/$error/$fatal`
   - `$finish/$stop`
   - `$dumpfile/$dumpvars`
   - `$fflush`
   - 统一生成 `kSystemTask`（旧 `kDisplay/kFwrite/kFinish` 已移除）
   - `kSystemTask` 需要携带 **过程块上下文**（避免 initial/final 被错误丢弃）：
     - `procKind`（Initial/Final/AlwaysComb/AlwaysLatch/AlwaysFF/Always）
     - `hasTiming`（是否显式 timing control）
     - `eventEdges/eventOperands`（边沿/敏感表，可为空）
   - Emit 侧按 `procKind` 选择 `initial/final/always_*` 形态；只有“应有事件却为空”时才警告
   - 语义约束与诊断：
     - `$dumpvars`：仅支持“全量 dump”（无参数或参数为 `0`），其它参数/选择列表给 warning 并忽略
     - `$sdf_annotate` / `$monitor` / `$monitoron` / `$monitoroff`：warning 并忽略
4) **Transform 规则**
   - const-fold / redundant-elim / CSE：**完全跳过** `kSystemFunction`（仅允许显式标注 `isPure` 的本地折叠）
   - `kSystemTask` 永不折叠/去重
5) **Emit 规则**
   - `kSystemFunction` 原样发射为 `$name(args...)`（必要时补 cast）
   - `kSystemTask` 原样发射为 `$name(args...)`（保留 guard 与 event 语义）
6) **迁移与移除（供评估）**
   - **已移除**：`kDisplay`、`kFwrite`、`kFinish`（统一转为 `kSystemTask`）
   - **已移除**：`kAssert`（后续如需 SVA 再单独建模）
   - **保留**：`kDpicCall`（不纳入 system task）
7) **覆盖范围（阶段化）**
   - Phase 1（仓库常用/关键）：`$display/$write/$strobe/$fwrite/$fdisplay/$info/$warning/$error/$fatal/$finish/$stop`
     + `$dumpfile/$dumpvars`、`$fflush`
     + `$bits/$clog2/$signed/$unsigned`、`$time/$stime/$realtime`、`$random/$urandom/$urandom_range`
   - Phase 2（可选/需类型支持）：`$fopen/$fclose/$ferror`（已支持）
     + 返回 string/real 的系统函数（如 `$sformatf`）已纳入（STEP 0053）

暂不支持：
- `$readmemh/$readmemb`（已迁移至 STEP 0052，以 `kMemory` 初始化属性处理）

文档与测试：
- 更新 `docs/GRH-representation.md`、`docs/convert/convert.md`（system function/task 分流表 + 迁移说明）
- 增加 convert/emit/transform 覆盖（含 `$random`/`$time`/`$bits`）
- 增加 system task 覆盖（优先补齐：`$dumpfile/$dumpvars`、`$fflush`、`$stop`）

实施：
- 已完成：
  - 新增 `kSystemFunction/kSystemTask` op 定义与名称注册
  - ExprLowerer：`$time/$stime/$realtime/$random/$urandom/$urandom_range` → `kSystemFunction`
  - StmtLowerer：支持常见 system task，统一落 `kSystemTask`（含 `procKind/hasTiming`）
  - Graph 组装：`kSystemFunction/kSystemTask` 属性与 operands 落地
  - Emit：`kSystemFunction` 表达式发射、`kSystemTask` 初始/组合/时序块发射
  - Transform：`kSystemFunction/kSystemTask` 视为有副作用，避免折叠/去重
  - 移除旧 op：`kDisplay/kFwrite/kFinish/kAssert`
  - 补充 `$fopen/$fclose/$ferror` system call 支持
  - `kSystemTask` 参数统一以 operands 表达（format/file/exitCode）
  - 文档：更新 `docs/convert/convert.md`
  - 文档：更新 `docs/GRH-representation.md`
  - 测试：补充 system function/task 覆盖

测试：
- 未运行

完成情况：已完成

## STEP 0053 - String/Real 类型支持（方案 B：完整类型）

目标：
- 为 `string/real` 引入 IR 级类型表达，并支持变量存储、赋值、比较与必要的系统函数
- 解决 “real/string 不能用 continuous assign 驱动” 的发射限制

计划（实现细化）：
1) **GRH/IR 类型系统**
   - `Value` 增加 `ValueType`（`Logic/Real/String`）
   - JSON/Load/Emit 输出携带 `ValueType`
   - `width/isSigned` 仅对 `Logic` 生效
2) **Convert 类型分类**
   - 新增 `classifyType(Type&) -> ValueType`
   - `ExprNode`/Plan 传播 `ValueType`
   - `string/real` 变量、端口、临时值均标注类型
3) **Emitter 发射策略**
   - `Logic` 保持现有 `assign`/net 生成
   - `Real/String` 生成变量声明（`real`/`string`），**禁止 continuous assign**
   - 为 `Real/String` 建立单独的赋值/组合块：
     - 组合值用 `always_comb` + 阻塞赋值
     - 避免 SSA 中间值的“assign”路径
4) **Expr/Op 支持范围**
   - `Real`：算术、比较、`$rtoi/$itor/$realtobits/$bitstoreal`
   - `String`：赋值、比较、系统任务/函数参数
   - 其它复杂运算（拼接/切片）保持诊断
5) **Transform 规则**
   - `Real/String` 值不参与 const-fold/CSE（除非显式纯函数）
   - `kSystemFunction` 继续视为有副作用
6) **测试与文档**
   - 增加 convert/emit 覆盖：`real t = $realtime;`, `string s = $sformatf(...)`
   - 更新 `docs/GRH-representation.md` 的类型章节

实施：
- GRH/IR 增加 `ValueType`，JSON/Load/Emit 带类型字段
- Convert 传播 `ValueType`，补充 `real/string` 字面量
- Emit 对 `Real/String` 走变量声明与 `always_comb` 赋值路径
- Transform/Pass 跳过 `Real/String` 的 const-fold/CSE
- 增补系统函数：`$sformatf/$psprintf/$itor/$rtoi/$realtobits/$bitstoreal`

测试：
- 未运行

完成情况：已完成

## STEP 0052 - $readmemh/$readmemb 作为 kMemory 初始化属性

目标：
- 以 **kMemory 属性**（而非 kSystemTask）表达 `$readmemh/$readmemb`
- 避免优化/重写内存时丢失初始化语义
- 统一 emit 出 initial 读文件行为

计划：
- Convert 侧识别 `$readmemh/$readmemb`：
  - 解析参数：`file`（字符串字面量）、`mem`（必须是 memory 符号）、可选 `start/finish`
  - 若在非 `initial`/`final` 过程块中出现，给 warning（或后续升级为 error）
- 将初始化信息挂到 memory 描述中（ModulePlan 或 Graph 组装时写入 kMemory）：
  - `initKind = readmemh/readmemb`
  - `initFile`
  - `initStart/initFinish`（可选）
  - 多次 init 允许按出现顺序记录（`initOrder`）
- GraphAssembler 在创建 kMemory op 时带上 init 属性
- Emit：
  - 根据 kMemory 的 init 属性生成 `initial $readmemh/$readmemb(...)`
  - 保证 memory 名称与 kMemory 对应 symbol 一致
- Transform：
  - memory 合并/替换时要求 init 属性一致，避免语义漂移

实施：
- 已完成：
  - Convert 解析 `$readmemh/$readmemb` 并记录为 memory init
  - GraphAssembler 将 init 属性写入 kMemory（`initKind/initFile/initStart/initFinish` + `initHasStart/initHasFinish`）
  - Emit 根据 kMemory 的 init 属性生成 `initial $readmemh/$readmemb(...)`
  - 非 `initial/final` 场景给 warning，按 init 处理
- Transform：新增 `MemoryInitCheckPass`，合并/替换场景下校验 init 属性一致性

测试：
- 新增包含 `$readmemh/$readmemb` 的 convert/emit 案例
- 覆盖 start/finish 可选参数

完成情况：已完成

## STEP 0054 - 可常量求值 system function 的折叠策略

目标：
- 凡是可静态求值的 system function，都在 convert 或 transform 阶段折叠为 `kConstant`
- `kSystemFunction` 只保留“运行期/有副作用”调用

计划：
1) **Convert 侧优先折叠**
   - 引入可折叠白名单（`$bits/$clog2/$size` 等）
   - 当参数全为常量时，直接求值生成 `kConstant`
2) **Transform 侧补漏**
   - 仅当 `hasSideEffects=false` 且 name 在白名单、operands 都是 `kConstant` 时允许折叠
   - 其余 `kSystemFunction` 永不折叠/去重
3) **System task 处理**
   - 不做“求值折叠”，只允许常量 guard 下的死语句清理（如 `if (0)` 任务块）

实施：
- Convert 侧：`$bits/$clog2/$size` 在参数可解析时折叠为 `kConstant`
- Transform 侧：对 `kSystemFunction` 中白名单函数（如 `$clog2`）在 operands 常量且 `hasSideEffects=false` 时折叠
- 其余 system function 保持为 `kSystemFunction`（不折叠）

测试：
- 未运行

完成情况：已完成

## STEP 0055 - Reg/Mem 初始化值属性（initValue）

目标：
- 为 kRegister/kMemory 等状态化 op 引入 `initValue` 属性，支持 initial 块初始化
- 解决 wolf-sv-parser 生成的代码中 Memory 未初始化导致 X 态的问题（CASE_007）
- 与现有 `$readmemh/$readmemb` 机制配合，按 initial 块顺序处理覆盖关系

计划：

1) **IR/GRH 扩展**
   - 为 kRegister/kMemory op 增加 `optional<string> initValue` 属性
   - `initValue` 格式支持：
     - `"0"` - 全 0 初始化
     - `"1"` - 全 1 初始化
     - `"$random"` - 无种子随机（`$random()`），每次调用独立
     - `"$random(N)"` - **带种子 N 的随机序列**：先执行 `$random(N)` 设置种子，后续用 `$random()` 继续序列
       - ⚠️ 注意：Verilog 中 `$random(seed)` 会**重置 RNG 状态**，连续调用相同 seed 会产生相同值
       - 正确用法：先 seed 一次，再生成序列
       ```verilog
       // initValue="$random(12345)" 发射为：
       initial begin
           $random(12345);  // 设置 seed
           for (i = 0; i < 32; i = i + 1)
               Memory[i] = $random();  // 继续序列，每个元素不同
       end
       // ❌ 错误：Memory[i] = $random(12345);  // 每次循环都 re-seed，所有元素相同！
       ```
     - `"8'hAB"` / `"8'b1010"` - 十六进制/二进制字面量
     - `"X"` / 省略 - 显式留 X（默认行为）
   - 支持为 unpacked array 的每个元素指定独立 initValue（如 memory 行）

2) **Convert 初始块解析（InitialBlockParser）**
   - 在 Pass1（SymbolCollector）或新增 Pass 中扫描 `initial` 过程块
   - 识别简单赋值模式：
     ```verilog
     // ✅ 支持：直接字面量赋值
     Memory[i] = 8'h00;
     Memory[i] = $random;
     Memory[i] = $random(12345);
     
     // ✅ 支持：全数组统一初始化（循环展开）
     for (i = 0; i < 32; i = i + 1)
         Memory[i] = $random;
     
     // ❌ 拒绝：复杂表达式
     Memory[i] = (cond) ? a : b;
     Memory[i] = func(arg);
     Memory[i] = OtherArray[j];  // 跨数组拷贝
     ```
   - 解析策略：
     - 单条赋值：直接提取 RHS 字面量或 `$random` 调用
     - `$random(seed)` 特殊处理：
       - 若在循环外单次调用：`initValue="$random(seed)"`
       - 若在循环内调用：需要区分是"每次迭代 seed"（罕见）还是"先 seed 再生成序列"
       - 默认假设：循环内的 `$random` 使用统一的 seed（循环前 seed）
     - 循环展开：静态求值循环边界，逐迭代提取赋值
     - 复杂 RHS：记录为 `"X"` 并发出 warning
   - 覆盖语义：按 initial 块中的语句顺序，**后来者覆盖前者**
     - 与 `$readmemh` 不强制优先级，统一按出现顺序处理
     - 示例：
       ```verilog
       initial begin
           $readmemh("init.hex", Memory);  // 先执行
           Memory[0] = 8'hFF;               // 覆盖 Memory[0]
       end
       // 结果：Memory[0]=8'hFF，其余来自文件
       ```

3) **与 readmemh 的整合**
   - 保留现有 STEP 0052 的 `kMemory.initKind = readmemh/readmemb` 机制
   - 扩展为统一的 `initSequence` 列表，按顺序记录：
     - `InitEntry{kind: readmemh/readmemb/literal/random, file/literal/seed, startAddr, endAddr}`
   - Convert 阶段：将 initial 块解析为 `initSequence`
   - GraphAssembler：将 `initSequence` 写入 kMemory 属性
   - 简化方案（MVP）：先支持单 initValue 或 readmemh 二选一，复杂序列后续扩展

4) **Emit 初始块生成**
   - 收集模块内所有带 initValue 的状态化 op（kRegister/kMemory）
   - 按 op 名称排序（保证确定性输出）
   - 生成单个 `initial begin ... end` 块：
     ```verilog
     initial begin
         // Register init
         reg_a = $random;  // initValue="$random"
         reg_b = 8'h00;    // initValue="8'h0"
         
         // Memory init
         for (integer i = 0; i < 32; i = i + 1)
             Memory[i] = $random;  // initValue="$random"
     end
     ```
   - 若存在 `$readmemh/$readmemb`，按 initSequence 顺序发射：
     ```verilog
     initial begin
         $readmemh("file.hex", Memory, 0, 31);
         Memory[0] = 8'hFF;  // 覆盖
     end
     ```

5) **复杂度控制与诊断**
   - 循环展开上限：复用 `maxLoopIterations` 配置（默认 65536）
   - 超过上限的初始化循环：
     - 发出 warning
     - 降级为 `"X"`（或部分初始化）
   - 复杂 RHS 表达式：
     - 发出 warning："Complex init expression not supported, leaving as X"
     - 设置 initValue="X"
   - 多驱动检测：
     - 若 initial 赋值与 always 块赋值冲突，保持现有诊断

6) **边界情况处理**
   - 二维数组初始化：`Memory[i][j]` 视为复杂，降级为 X
   - 部分初始化：未初始化元素保持 X
   - 参数依赖：`Memory[i] = PARAM` - 若 PARAM 为常量，求值记录；否则 X

实施：
- 待定：优先完成基础支持（全 0 / 全随机 / 字面量），复杂序列后续迭代

测试：
- 新增 CASE_007 回归测试：验证 Memory 初始化后无 X
- 覆盖：零初始化、随机初始化、字面量、循环展开、readmemh 后覆盖

完成情况：待开始

