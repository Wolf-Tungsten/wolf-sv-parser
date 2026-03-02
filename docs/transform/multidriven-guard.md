# multidriven-guard

## 功能概述

`multidriven-guard` pass 在层次结构扁平化之前检测多驱动冲突，特别是针对双向（inout）端口的驱动冲突。

## 详细说明

在层次化设计中，双向端口可能在多个层级被驱动。该 pass 通过分析子模块内部的驱动情况，检测可能导致冲突的驱动源。

### 检测机制

1. **驱动信息收集**：
   - 分析每个模块中的输出和输出使能（OE）驱动
   - 通过实例边界递归传播驱动信息

2. **冲突检测**：
   - 比较父模块中已存在的驱动与子模块中的驱动
   - 检查双向端口的 `out` 和 `oe` 信号

3. **错误报告**：
   - 报告冲突的端口名称
   - 提供父模块和子模块中的驱动源信息
   - 包含实例路径和模块名称

## 使用场景

- 在 `hier-flatten` 之前运行，提前发现潜在的驱动冲突
- 验证多顶层设计的正确性
- 检查 IP 核集成时的端口连接

## 使用示例

```bash
# 在扁平化之前运行
wolvrix --pass=multidriven-guard --pass=hier-flatten input.sv
```

## 错误信息格式

```
multidriven-guard: inout out already driven before hier-flatten
  port=<port_name>
  parent_value=<signal_name>
  parent_def=<driver_op>
  child_def=<driver_op>
  child_def_graph=<module_name>
  child_def_path=<internal_path>
  child_module=<module_name>
  instance=<instance_name>
  graph=<graph_name>
```

## 注意事项

- 该 pass 不会修改 IR，仅进行检测和报告
- 发现冲突会导致 pass 失败（`result.failed = true`）
- 递归实例的驱动分析会被跳过
- 建议在每次层次结构修改后重新运行
