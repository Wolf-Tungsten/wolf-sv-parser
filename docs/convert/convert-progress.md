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
- while/do-while/forever 直接报错并停止对其 body 的降级

计划：
- 将相关分支的诊断从 todo 升级为 error
- 对 pattern 条件/PatternCase/while/do/forever 采用 “报错 + 跳过” 策略
- 更新 workflow/architecture 说明不支持范围
- 增补错误路径测试

实施：
- StmtLowerer 将 pattern condition 与 pattern case 升级为 error 并直接跳过分支
- while/do-while/forever 直接报错并停止对 body 的降级
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
- 待实施

完成情况：未开始

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
- 待实施

完成情况：未开始

## STEP 0021 - Pass5 测试覆盖扩展

目标：
- 覆盖 case inside、pattern 条件报错、while/do/forever 报错、展开上限、位选/范围选 LHS

计划：
- 扩展 `tests/data/convert/stmt_lowerer.sv` 增加新模块用例
- 在 `tests/convert/test_convert_stmt_lowerer.cpp` 增加断言与诊断验证
- 需要时新增独立测试目标以区分错误路径与正常路径

实施：
- 待实施

完成情况：未开始
