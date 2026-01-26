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
