# 项目结构改造方向记录

## 需求汇总
- 项目名称更改为 `wolvrix`（暂不修改现有代码与文件，仅记录后续统一替换范围与影响）。
- 源码结构区分 `lib` 与 `app` 两个部分：
  - `lib`：存放核心组件/可复用逻辑。
  - `app`：基于核心组件的可执行应用。
- pass 概念不再局限于 transform，结构上不单独设置 `pass/` 目录。
- `lib` 中保留常用的 transform pass（现有 pass 视为核心能力）。
- `app` 中允许用户按需开发新的 transform pass（面向特定应用/场景）。
- `include/` 迁入 `lib/` 范围内统一管理。
- 每个 `app/` 目录下自带自身的 header 与 cpp 实现文件。
- 命名空间根统一为 `wolvrix`：
  - `lib` 下内容置于 `wolvrix::lib`。
  - `app` 下内容置于 `wolvrix::app`。
- `grh` 下不再设置 `ir` 子层级；现有 IR 核心组件归入 `wolvrix::lib::grh`。
- 现有四大组件按命名空间组织为：
  - `wolvrix::lib::ingest`（原 convert）
  - `wolvrix::lib::load`
  - `wolvrix::lib::transform`
  - `wolvrix::lib::emit`
- 组件命名调整：
  - `wolvrix::lib::ingest`：负责将 SV（或来自解析器的 AST）导入为 GRH。
  - 新增 `wolvrix::lib::store`：负责将 GRH 落盘为 JSON。
  - `wolvrix::lib::emit`：专注将 GRH 输出为 SV 等目标格式。
  - `wolvrix::lib::load`：保留用于 JSON -> GRH。
  - `emit` 命名继续沿用，不改动。
- 现有 `wolf-sv-parser` 归入 `wolvrix::app::cli`（命名空间更简洁清晰）。
- 编译产出的应用名称为 `wolvrix-cli`。
- `wolvrix::lib` 需可打包为动态链接库，作为 SDK 提供给第三方开发者使用。
- 命名策略采用按子系统命名空间隔离：优先使用 `wolvrix::lib::transform::Pass`（以及未来可能的 `wolvrix::lib::<subsystem>::Pass`），避免引入冗长的 `TransformPass` 命名。

## 推进计划
1) **命名与定位定稿**
   - 项目名统一为 `wolvrix`，CLI 应用名为 `wolvrix-cli`。
   - 命名空间根为 `wolvrix`，库与应用分别为 `wolvrix::lib` / `wolvrix::app`。
   - 子系统命名：`wolvrix::lib::{grh, ingest, transform, emit, load, store}`。
2) **目录结构重组**
   - 引入 `lib/` 与 `app/` 顶层目录。
   - 将现有 `include/` 迁入 `lib/` 统一管理。
   - 每个 `app/` 目录自带 header 与 cpp 文件。
3) **GRH 与子系统归位**
   - 移除 `grh/ir` 子层级，IR 核心组件归入 `wolvrix::lib::grh`。
   - `ingest` 负责 SV/AST -> GRH，`emit` 负责 GRH -> SV。
   - 新增 `store` 负责 GRH -> JSON，`load` 负责 JSON -> GRH。
4) **Transform pass 组织**
   - `wolvrix::lib::transform` 内保留常用 pass。
   - 通过 `Pass/PassManager/PassRegistry/PassContext` 统一接口。
   - pass 实现文件放 `lib/transform/`，不设独立 `pass/` 目录。
   - app 侧可注册自定义 pass，形成可扩展 pipeline。
5) **App 落地**
   - 现有 `wolf-sv-parser` 迁入 `wolvrix::app::cli`。
   - 产出 `wolvrix-cli` 作为首个 app。
6) **SDK 输出能力**
   - `wolvrix::lib` 支持构建为动态链接库，供第三方 SDK 使用。
7) **文档与验证**
   - 记录结构性改动并保持计划更新。
   - 维护最小可行迁移路径，确保构建与测试持续通过。
