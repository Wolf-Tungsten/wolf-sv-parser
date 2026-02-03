# GRH IR 开发进展跟踪

## 整体目标

建立 Graph/GraphBuilder 的性能基线，明确热点路径与瓶颈来源，围绕 value 映射、use 替换、缓存与遍历的关键热区做结构性优化，并用可重复的 micro-bench + 真实 workload（c910）验证收益。

## 文档 SOP

- docs/grh/grh-progress.md（本文档）：进展文档，增量式记载每个 step 的计划与实施情况，在分割线后每次增加一个 STEP XXXX 章节；如果后续步骤推翻了前序设计，则前序文档不删除，以 markdown 删除线形式标注。
- docs/grh/ir-hot-path-design.md：热点与性能设计文档，记录 Graph/GraphBuilder 的性能约束、关键数据结构与优化策略；该文档与代码保持强一致性，变更直接覆盖。



## STEP 0001 - Graph 热点复盘与基线建立

目标：
- 基于 c910 gprof 结果定位 Graph 相关热点与调用链（valueFromBuilder/replaceAllUses/ensureCaches）
- 建立可复现的性能基线（输入规模、编译参数、运行选项与采样方式）
- 明确后续优化的度量指标与验收口径

计划：
- 补充 gprof call graph 分析，标出热点在 Graph/GraphBuilder 中的具体入口与高频循环
- 记录基线指标：总耗时、前 20 热点、自身/累计时间占比
- 固化基线信息到文档（含输入规模、编译 flags、运行命令）

实施：
- 待开始

完成情况：未开始


## STEP 0002 - Graph::valueFromBuilder 映射优化

目标：
- 降低 builder->graph ValueId 映射成本，减少重复查找
- 将映射从“多点查询”收敛为“单次构建/按阶段缓存”

计划：
- 盘点 valueFromBuilder 的调用点与频次，确认是否可在批处理阶段完成映射
- 评估按图缓存（Graph 内一次性建立映射表）与按构建阶段缓存（GraphBuilder 产出时携带映射）的实现复杂度
- 增加 micro-bench 验证映射成本与 cache 命中收益

实施：
- 待开始

完成情况：未开始


## STEP 0003 - GraphBuilder::replaceAllUses 批处理优化

目标：
- 减少 replaceAllUses 的全量扫描与 ValueId 比较次数
- 引入 use-list 或批量替换路径，支持多 value 合并替换

计划：
- 盘点 replaceAllUses 的调用场景，区分“单点替换”与“批量替换”
- 评估 use-list（ValueId -> users）或索引结构（OperationId + operand index）的成本与收益
- 若引入 use-list，补齐 GraphBuilder 维护逻辑并提供批量替换接口

实施：
- 待开始

完成情况：未开始


## STEP 0004 - Graph::ensureCaches 去抖与惰性更新

目标：
- 避免多次重复构建缓存，降低 ensureCaches 的总开销
- 引入修改 epoch 与按需重建策略

计划：
- 标记 Graph/GraphBuilder 的“结构变更点”，维护全局修改计数
- 为各 cache 增加 last_built_epoch，只有在首次查询且过期时才重建
- 梳理 ensureCaches 的调用点，合并或上移到批处理阶段

实施：
- 待开始

完成情况：未开始


## STEP 0005 - Graph/GraphBuilder 遍历与容器访问微优化

目标：
- 降低热点循环中 `vector::size()` / `operator[]` 的调用频次
- 减少不必要的迭代器构造与短生命周期对象开销

计划：
- 在热点路径中 hoist `size()`、`data()` 与边界计算
- 优先使用 span/指针遍历，避免多层 iterator 包装
- 结合热点函数局部 micro-bench 验证收益

实施：
- 待开始

完成情况：未开始


## STEP 0006 - Graph 数据布局与内存行为优化

目标：
- 改善 Value/Operation 的 cache 局部性与访问密度
- 在不破坏 API 的前提下减少对象膨胀与拷贝成本

计划：
- 评估 ValueData/OperationData 的字段排列与 SoA/packed 布局
- 评估 ID/索引类型的紧凑化（如 32-bit）与批量分配策略
- 若需要引入结构变更，同步更新 `docs/grh/ir-hot-path-design.md`

实施：
- 待开始

完成情况：未开始
