# Transform Pass 开发手册

本文描述在当前框架下新增一个 transform pass 的流程与约定，涵盖文件放置、编码风格、参数配置、诊断/中断策略以及注册/测试要点。

## 文件放置与命名
- 头文件放在 `lib/include/transform/`，实现放在 `lib/transform/`，文件名尽量简短且贴合职责，例如 `foo_opt.hpp` / `foo_opt.cpp`。
- 类名使用 `PascalCase`，继承基类 `wolvrix::lib::transform::Pass`，构造函数接受必要的配置参数（不再使用统一的 `PassConfig`）。
- 默认命名空间：`wolvrix::lib::transform`。
- `id` 由子类构造设置，标记 pass 类型；`name` 在 `PassManager::addPass` 时设置，未显式提供时默认等于 id，用于区分同类 pass 的多次实例化（诊断前缀使用 name）。

## Skeleton 与参数配置
示例骨架：
```cpp
// lib/include/transform/foo_opt.hpp
#pragma once
#include "transform.hpp"

namespace wolvrix::lib::transform {

    struct FooOptOptions {
        bool enableFoo = true;
        int maxIterations = 4;
    };

    class FooOptPass : public Pass {
    public:
        explicit FooOptPass(FooOptOptions opts = {});
        PassResult run() override;

    private:
        FooOptOptions options_;
    };

} // namespace wolvrix::lib::transform
```
```cpp
// lib/transform/foo_opt.cpp
#include "transform/foo_opt.hpp"

namespace wolvrix::lib::transform {

    FooOptPass::FooOptPass(FooOptOptions opts)
        : Pass("foo-opt", "Foo Optimizer", "Optimize foo patterns"), options_(opts) {}

    PassResult FooOptPass::run() {
        PassResult result;
        // TODO: mutate netlist() / graphs here
        // emit diagnostics via diags()
        return result;
    }

} // namespace wolvrix::lib::transform
```
- 参数通过构造参数传入，必要时定义 options 结构体；保持默认值合理，便于调用方直接构造。

## 诊断与中断策略
- 基类提供便捷访问器：`netlist()`、`diags()`、`verbosity()`，无需在 run 传参。
- 直接在 Pass 内调用辅助方法（自动带上 pass id），低于 `PassContext::verbosity` 的诊断会被过滤，不会写入 `PassDiagnostics`：
  - `debug/info/warning/error(message, context = {})`。
  - `debug/info/warning/error(graph, op|value|graph, message)` 自动生成 `graph::op::val` 上下文。
- `PassResult`：
  - `changed = true` 表示已修改 Netlist/Graph。
  - `failed = true` 表示该 pass 自身失败（即使没有 error 诊断）。通常在遇到不可恢复状态时置位。
- `PassManager` 行为：
  - 如果 `options.stopOnError == true`，遇到 `failed` 或已有 error 诊断会短路后续 pass。
  - 返回的 `PassManagerResult::success` 只有在没有 error 且未遇到 failed 时才为 true。
- 诊断级别：`Debug < Info < Warning < Error`。`PassManagerOptions::verbosity`/`PassContext::verbosity` 控制最低保留级别（默认 `Info`，仅屏蔽 `Debug`）。

## Scratchpad 跨 pass 共享
- `PassContext` 暴露 `scratchpad`（`std::unordered_map<std::string, std::unique_ptr<ScratchpadSlot>>`），每个条目堆分配，移除后立即释放。基类封装了访问器：
  - `setScratchpad(key, value)` 支持传值/移动，优先传 `std::move(heavyObj)` 或直接存 `std::unique_ptr<T>` 以避免大对象拷贝。
  - `getScratchpad<T>(key)` 返回指向存储对象的指针，类型不匹配时返回 `nullptr`。
  - `hasScratchpad(key)` / `eraseScratchpad(key)` 检查或移除条目。
- 生命周期：`scratchpad` 在每次 `PassManager::run` 开始时创建，run 结束后整个 map 被销毁；`eraseScratchpad`/`clear` 可提前释放单项。
- 命名建议：用 `<pass-id>/<name>` 形式避免冲突，例如 `setScratchpad("constprop/stats", Stats{...});`，下游读者检查指针非空再使用。
- 示例（写入/读取整数计数）：
```cpp
class CountProducer : public Pass {
    PassResult run() override {
        setScratchpad("counting/ops", netlist().graph("top").operations().size());
        return {};
    }
};
class CountConsumer : public Pass {
    PassResult run() override {
        if (const size_t* n = getScratchpad<size_t>("counting/ops")) {
            info("ops from producer: " + std::to_string(*n));
        } else {
            warning("count missing in scratchpad");
        }
        return {};
    }
};
```

## 注册与集成
- 在 CMake 中将新实现编译入 `wolvrix-lib`：添加到 `CMakeLists.txt` 的 `WOLVRIX_LIB_SOURCES` 源文件列表。
- 在需要的入口注册，可按实例命名：
  - CLI 主流程示例（`app/cli/main.cpp`）：`passManager.addPass(std::make_unique<FooOptPass>(FooOptOptions{/*...*/}), "foo-opt:phase1");`，未显式提供 name 时默认使用 pass 的 id。
  - 测试中同样通过 `addPass` 推入 pipeline。
- 若未来引入 registry/CLI 解析，可在构造层接受来自命令行的选项，再交给 pass 构造函数。

## 编码风格与最佳实践
- 遵循仓库 C++20 风格（4 空格缩进、头文件最小化、同一行 brace）。
- 仅使用 `netlist()` 中的图/节点指针；引用解析应受 graph 范围约束，除非语义要求跨 graph（如 DPI import）。
- 对外部输入进行充分的合法性检查；在修复缓存/填充缺省值时同步记录 `changed` 并发出 `info`/`warning`。
- 遇到不可修复的结构缺陷（缺失 symbol、未知 kind 等）要及时 `error` 并考虑 `failed = true`。

## 测试建议
- 为每个新 pass 添加最小化单测，覆盖：
  - 正常路径（无诊断，或仅 info/warn）。
  - 错误路径（error/failed 短路行为）。
  - 状态变化（`changed` 标志、输出 graph 变形）。
- 测试目标放在 `tests/transform/`，在 `CMakeLists.txt` 中注册为独立可执行并加入 `ctest`。
