# Python Binding Session 化重构草案

## 1. 一句话定义

`Session` 就是一个统一的 key-value 空间。

没有 `scratchpad` 这个额外层次。  
以后：

- `Design` 是一个 value
- 计划是一个 value
- 分区信息是一个 value
- 统计信息是一个 value
- 调试快照也是一个 value

## 2. 最小模型

session 对外只需要这个模型：

```text
key -> value
```

value 最多带一个 `kind`。

也就是：

```text
key -> (kind, value)
```

除此之外，不在公开设计里引入更多结构。

像 `version`、`meta`、`payload` 这种东西，如果实现时需要，可以放到内部，但不应该成为这份草案的核心概念。

## 3. 设计原则

1. `with wolvrix.Session() as sess:` 是唯一标准入口。
2. 所有 ingest / load / transform / emit / store 都围绕 session key 工作。
3. transform 默认原地修改 design。
4. 需要保留分支时，显式 clone design。
5. pass 内部原先依赖的 scratchpad，目标上改成直接读写 session。
6. session 必须易扩展。新增一种中间数据，不应该重做 session 框架。
7. session 里的数据不只是给 Python 调试看的，更是 C++ 内部 pass/pass、pass/emit 之间的标准传递方式。

## 4. key 约定

key 只是名字，不要再区分什么“session key”和“scratchpad key”。

例如：

- `design.main`
- `design.flat`
- `tkd.plan.main`
- `tkd.groups.main`
- `repcut.partition.top`
- `stats.main`
- `debug.before`
- `debug.after`

这些在 session 里地位完全一样，区别只在 `kind` 和具体内容。

## 5. kind 约定

`kind` 是开放字符串，不要做成封闭枚举。

例如：

- `design`
- `json`
- `sv`
- `tkd.plan`
- `tkd.groups`
- `repcut.partition`
- `stats`

重点不是名字本身，而是：

- session 核心不预设只有哪几种合法对象
- 新增一种数据时，只要给它一个 `kind` 就能放进去

## 5.1 session 的主要用途

需要特别强调：

session 里的数据不只是给 Python 侧调试看的。

更重要的是：

- 一个 pass 可以把结果写到 session
- 下一个 pass 可以继续吃这个 key
- emit 也可以吃这个 key
- 这一整条链在 C++ 侧要成立，Python 只是负责把 key 传进去

例如：

```python
sess.run_pass(
    "tkd_sched",
    design="design.dut",
    tkdresultkey="tkd.result.dut",
)

sess.emit_cpp(
    path="build/out.cpp",
    design="design.dut",
    tkdresultkey="tkd.result.dut",
)
```

这里的 `tkd.result.dut` 不是为了 Python inspect 才存在的，  
它首先是 C++ 侧阶段间传递的数据。

## 6. API 规划表

以下表格作为当前草案的主视图。  
后面的文字说明都以这张表为准。

| 现有 API | 计划 API | 处理方式 | 备注 |
| --- | --- | --- | --- |
| `Design` | 无独立公开构造；design 作为 session value 存在 | 删除旧对象入口 | Python 侧如需查看 design，可提供 `sess.design(design_key)` adapter |
| `read_sv(path, slang_args=None, ...) -> (Design \| None, diag)` | `sess.read_sv(path, *, target_design_key: str, slang_args=None, replace=False) -> list[dict]` | 替换 | 创建或替换一个 `kind="design"` 的 session value |
| `read_json(path) -> Design` | `sess.read_json_file(path, *, target_design_key: str, replace=False) -> list[dict]` | 替换 | 从 JSON 文件加载 design 到 session |
| `from_json_string(text) -> Design` / `load_json_string(text)` | `sess.load_json_text(text, *, target_design_key: str, replace=False) -> list[dict]` | 替换 | 从 JSON 文本加载 design 到 session |
| `Design.run_pass(name, args=None, ...)` / `run_pass(design, name, ...)` | `sess.run_pass(name: str, *, design: str, args=None, dryrun=False, ...) -> list[dict]` | 替换 | `design` 参数是 design key；其他 session key 采用显式命名参数 |
| `Design.run_pipeline(...)` / `run_pipeline(...)` | 无 | 删除 | 多步 transform 直接写成多次 `sess.run_pass(...)` |
| `Design.write_sv(output, top=None, split_modules=False)` / `write_sv(design, output, ...)` | `sess.emit_sv(*, design: str, output: str, top=None, split_modules=False, ...) -> list[dict]` | 替换 | 主输出直接写盘 |
| `Design.write_verilator_repcut_package(output, top=None)` / `write_verilator_repcut_package(...)` | `sess.emit_verilator_repcut_package(*, design: str, output: str, top=None, ...) -> list[dict]` | 替换 | 主输出直接写盘 |
| 无 | `sess.emit_cpp(*, design: str, path: str, ...) -> list[dict]` | 新增（但本次不需要实现） | 用于需要消费 session 中间数据的 C++ emit |
| `Design.to_json(mode="pretty-compact", top=None) -> str` / `store_json_string(design, ...) -> str` | 暂不保留为首批公开 API | 暂缓 | 大输出场景优先走 `store_json` 写盘；如确有必要，后续再加显式 debug helper |
| `Design.write_json(output, mode="pretty-compact", top=None)` | `sess.store_json(*, design: str, output: str, mode="pretty-compact", top=None) -> list[dict]` | 替换 | 主输出直接写盘 |
| `list_passes() -> list[str]` | `wolvrix.list_passes() -> list[str]` 或 `sess.list_passes() -> list[str]` | 保留 | 倾向保留顶层只读查询 |
| 无 | `sess.clone_design(src: str, dst: str, *, replace=False) -> list[dict]` | 新增 | 显式 design 分叉 |
| 无 | `sess.get(key)` / `sess.put(key, value, *, kind, replace=False)` | 新增 | `get` 默认返回通用实体；如该 kind 存在 adapter，可再提供专用访问 |
| 无 | `sess.kind(key) -> str` / `sess.keys(...) -> list[str]` | 新增 | session 检索接口 |
| 无 | `sess.delete(key)` / `sess.rename(src, dst)` / `sess.copy(src, dst)` | 新增 | session 管理接口 |
| 无 | `sess.set_diagnostics_policy(level)` / `sess.diagnostics_policy()` | 新增 | session 统一诊断响应策略 |
| 无 | `sess.set_log_level(level)` / `sess.log_level()` | 新增 | session 统一 C++ log 策略 |
| 无 | `sess.history() -> list[dict]` | 新增 | 查看 action 历史与 diagnostics |

## 7. API 草案

## 7.1 Session 基础接口

```python
class Session:
    def __enter__(self) -> "Session": ...
    def __exit__(self, exc_type, exc, tb) -> None: ...
    def close(self) -> None: ...

    def __contains__(self, key: str) -> bool: ...
    def keys(self, prefix: str | None = None, kind: str | None = None) -> list[str]: ...
    def kind(self, key: str) -> str: ...

    def get(self, key: str): ...
    def put(self, key: str, value, *, kind: str, replace: bool = False) -> None: ...

    def delete(self, key: str) -> None: ...
    def rename(self, src: str, dst: str, *, replace: bool = False) -> None: ...
    def copy(self, src: str, dst: str, *, replace: bool = False) -> None: ...

    def set_diagnostics_policy(self, level: str) -> None: ...
    def diagnostics_policy(self) -> str: ...

    def set_log_level(self, level: str) -> None: ...
    def log_level(self) -> str: ...

    def history(self) -> list[dict]: ...
```

这里最重要的是：

- `get`
- `put`
- `copy`

这是 session 作为统一数据空间的最小闭环。

除此之外，session 还需要统一管理两项运行策略：

- diagnostics 响应等级
- C++ log 等级

这两项属于 session 配置，不应该要求每个 action 重复传参。

## 7.2 action 返回值

不再保留 `ActionResult`。

每个 action 直接返回 diagnostics：

```python
diagnostics: list[dict]
```

约束：

- 每个 action 一定返回 diagnostics
- 没有诊断时返回空列表，不返回 `None`
- diagnostics 结构直接对接现有 C++ diagnostics 系统

也就是说：

```python
diag = sess.run_pass("simplify", design="design.main")
diag = sess.read_sv("top.sv", target_design_key="design.main")
diag = sess.emit_sv(design="design.main", output="build/main.sv")
```

## 7.3 diagnostics policy

session 统一配置 diagnostics 响应等级，例如：

```python
with wolvrix.Session() as sess:
    sess.set_diagnostics_policy("error")
```

建议支持：

- `none`
- `warning`
- `error`

推荐语义：

- `none`: 永不因 diagnostics 抛异常
- `warning`: 只要出现 warning/error/todo 就抛异常
- `error`: 只要出现 error/todo 就抛异常

执行顺序应当是：

1. action 完成
2. 收集 diagnostics
3. 返回 diagnostics
4. 根据 session policy 决定是否抛异常

这样 diagnostics 始终完整产生，异常只是后处理动作。

## 7.4 log policy

session 统一配置 log level，例如：

```python
with wolvrix.Session() as sess:
    sess.set_log_level("warn")
```

这里的 log 先限制在 C++ 层：

- 控制 native ingest / transform / emit / store 的日志输出
- 不回传到 Python
- 不进入返回值

所以这次要明确区分：

- diagnostics: 结构化结果，回到 Python
- log: C++ 运行时输出，不回到 Python

## 7.5 设计对象入口

```python
sess.read_sv(path, *, target_design_key="design.main", slang_args=None, replace=False) -> list[dict]
sess.read_json_file(path, *, target_design_key="design.main", replace=False) -> list[dict]
sess.load_json_text(text, *, target_design_key="design.main", replace=False) -> list[dict]
```

这些动作都是往 session 里放一个 `kind="design"` 的 value。

它们的 diagnostics 都直接返回，并受 session diagnostics policy 约束。

## 7.6 design 分叉

```python
sess.clone_design(src: str, dst: str, *, replace: bool = False) -> list[dict]
```

必须显式保留这个接口，因为 design clone 是重操作，不该隐含在一般 API 里。

## 7.7 Transform

```python
sess.run_pass(
    name: str,
    *,
    design: str,
    args: list[str] | None = None,
    dryrun: bool = False,
) -> list[dict]
```

语义：

- `design` 是被修改的 design key
- 默认原地修改
- diagnostics 直接作为返回值
- log level 统一取 session 配置

不再提供 `pipeline` API。

原因：

- session 已经是统一数据空间
- 多步 transform 直接写成多次 `run_pass(...)` 更清楚
- 每一步 diagnostics、异常、session 输出都独立可见
- 不需要再额外维护一层“组合式 transform 接口”

另外，transform API 采用显式命名参数风格。

也就是说：

- 不提供 `inputs={...}` / `outputs={...}`
- 不提供 `**session_keys`
- 某个 pass 需要哪些 session key，就在这个 pass 的接口契约里直接写成命名参数

例如 `tkd_sched` 的实际调用形态应当是：

```python
sess.run_pass(
    "tkd_sched",
    design="design.main",
    tkdresultkey="tkd.plan.main",
    tkdgroupskey="tkd.groups.main",
)
```

而不是：

```python
sess.run_pass(
    "tkd_sched",
    design="design.main",
    session_keys={
        "result": "tkd.plan.main",
    },
)
```

这里的意思是：

- Python 侧显式指定 session key
- C++ pass 按固定参数名接收这些 key
- pass 直接把结果写入这些 key
- 参数名本身就是 pass 契约的一部分

## 7.8 Emit / Store

默认仍然直接写盘，但允许读 session 里的附加值：

```python
sess.store_json(
    *,
    design: str,
    output: str,
    mode: str = "pretty-compact",
    top: list[str] | None = None,
) -> list[dict]

sess.emit_sv(
    *,
    design: str,
    output: str,
    top: list[str] | None = None,
    split_modules: bool = False,
) -> list[dict]

sess.emit_cpp(
    *,
    design: str,
    path: str,
) -> list[dict]
```

这里：

- `output` 是主输出路径
- `design` 是输入 design key
- diagnostics 直接作为返回值
- log level 统一取 session 配置

约束：

- `emit/store` 不负责把大产物放回 session
- session 不是大文件缓存层
- 如果 Python 侧确实要查看结果，直接用文件 API 读 `output` 即可

和 pass 一样，emit 侧也采用显式命名参数风格。

例如 `emit_cpp` 需要消费 TKD 结果时，调用形态应该是：

```python
sess.emit_cpp(
    design="design.main",
    path="build/main.cpp",
    tkdresultkey="tkd.plan.main",
)
```

而不是再套一层通用映射参数。

也就是说，具体 emitter 需要哪些 session key，就把这些 key 直接体现在它自己的命名参数里。

## 8. 推荐用法

## 8.1 正常流程

```python
with wolvrix.Session() as sess:
    sess.read_sv("top.sv", target_design_key="design.main")
    sess.run_pass("simplify", design="design.main")
    sess.store_json(design="design.main", output="build/main.json")
    sess.emit_sv(design="design.main", output="build/main.sv")
```

## 8.2 有中间结果

```python
with wolvrix.Session() as sess:
    sess.read_sv("top.sv", target_design_key="design.main")
    sess.run_pass("blackbox-guard", design="design.main")
    sess.run_pass("xmr-resolve", design="design.main")

    sess.run_pass(
        "tkd_sched",
        design="design.main",
        tkdresultkey="tkd.plan.main",
        tkdgroupskey="tkd.groups.main",
    )

    sess.emit_cpp(
        design="design.main",
        path="build/main.cpp",
        tkdresultkey="tkd.plan.main",
    )
```

## 8.3 保留分支

```python
with wolvrix.Session() as sess:
    sess.read_sv("top.sv", target_design_key="design.raw")
    sess.clone_design("design.raw", "design.flat")
    sess.run_pass("hier-flatten", design="design.flat")
```

## 9. Python 访问方式

首先必须有通用访问：

```python
value = sess.get("tkd.plan.main")
kind = sess.kind("tkd.plan.main")
```

这里的 `sess.get(key)` 不要求把所有 kind 都在 Python 侧深入解构。

推荐策略：

- 如果某个 kind 有 Python adapter，就返回对应的友好访问接口
- 如果没有 adapter，就返回一个通用的不可知实体

例如可以是：

```python
value = sess.get("tkd.plan.main")
value.kind
value
```

这个通用实体只需要保证几件事：

- 它能表明自己的 `kind`
- 它能作为一个稳定句柄在 Python 里继续传递
- Python 不需要理解它的内部结构也能继续把它留在 session 里

也就是说，不是所有 session value 都必须在 Python 侧可深读。

这点很重要，因为 session 首先是 C++ 阶段间的数据通道，其次才是 Python 观察面。

然后再给核心 kind 补专用访问：

```python
design = sess.design("design.main")
```

专用 adapter 是增强，不是模型本体。  
没有 adapter 的 value，Python 侧就接受它是一个不可知实体。  
模型本体始终只是 `key -> (kind, value)`。

## 10. C++ 侧目标

C++ 侧目标也应该按这个最小模型来：

1. session 是一个统一的 key-value store
2. `PassContext::scratchpad` 最终被 session 读写替代
3. pass/emit 不再面对私有 scratchpad，而是直接面对 session 中的 key
4. 新增中间数据类型时，不改 session 对外模型

实现上可以有内部辅助结构，但不应该把内部结构上升成 API 设计。

## 11. 结论

这次重构最该坚持的是这件事：

> session 不是复杂对象系统，  
> 它就是统一的 key-value 空间，最多给每个 value 带一个 kind。

这样才不会把问题重新做复杂，也最符合“session 就是 scratchpad”这个目标。
