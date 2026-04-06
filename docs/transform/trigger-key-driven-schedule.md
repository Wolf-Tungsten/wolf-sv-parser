# trigger-key-driven-schedule

`trigger-key-driven-schedule` builds a static scheduling plan for one graph and stores that plan in the session.

This pass is for planning, not for rewriting the graph into a new IR form. Its job is to compute data that later passes or emitters can reuse.

## Why This Pass Exists

Some downstream flows do not want to recompute the full logic cone on every step. Instead, they want a precomputed plan that says:

- which groups of operations behave as sinks
- which groups are driven by trigger signals
- which groups are ordinary combinational work
- what the dependency order is between those groups

That is exactly what this pass produces.

## Python Example

```python
with wolvrix.Session() as sess:
    sess.read_sv("top.sv", out_design="design.main")
    sess.run_pass(
        "trigger-key-driven-schedule",
        design="design.main",
        path="top",
        out_tkd_schedule="tkd.plan.main",
        out_tkd_groups="tkd.groups.main",
        out_tkd_meta="tkd.meta.main",
    )
```

Read the arguments like this:

- `design="design.main"`: analyze this design
- `path="top"`: choose which graph inside the design to analyze
- `out_tkd_schedule=...`: store the main schedule result here
- `out_tkd_groups=...`: store group and pool data here
- `out_tkd_meta=...`: store summary metadata here

## Path Semantics

The `-path` option supports two forms:

- a single graph name
- a hierarchical instance path such as `<root>.<inst>...`

Multi-segment paths are resolved by instance name, not by module name:

1. the first segment selects the root graph
2. each later segment matches an `instanceName`
3. once an instance matches, its `moduleName` becomes the next graph

Examples:

- `SimTop.logic_part`
- `top.u_core.u_dut`

## Preconditions

This pass works on exactly one target graph.

It is usually best to run it after hierarchy and XMR cleanup. In practice, that usually means after:

- `xmr-resolve`
- hierarchy normalization or flattening as needed

The target graph is expected not to contain:

- `kInstance`
- `kBlackbox`
- `kXMRRead`
- `kXMRWrite`

The pass also records `ValueId` and `OperationId` values. Those identifiers are only stable for the current graph structure, so if the graph changes significantly later, the previously generated schedule should be considered stale.

## What The Pass Produces

The schedule is built around three kinds of groups:

- `SinkTKDGroup`
- `TriggerTKDGroup`
- `SimpleTKDGroup`

It also computes:

- a `TriggerKey` pool
- an `AffectedSinkSet` pool
- an `OperationId -> TkdGroupId` index
- group-level dependency edges
- a topological order over groups

Downstream consumers can use this data to decide which logic really needs to be evaluated.

## Session Outputs

The pass writes results to the session.

Explicit outputs:

- `out_tkd_schedule`
  - main schedule result, kind `tkd.result`
- `out_tkd_groups`
  - group, pool, and index data, kind `tkd.groups`
- `out_tkd_meta`
  - metadata and counts, kind `tkd.meta`

If you do not provide explicit `out_*` keys, the current implementation still writes a more fine-grained set of default keys under:

```text
tkds/<modulePath>/...
```

Those keys are ordinary session values. They do not live in a separate side channel.

## Important Notes

- this pass mainly produces session values
- it does not rewrite the target graph into a new planned graph
- path matching uses instance names, not module names
- if later passes heavily rewrite the graph, regenerate the TKD data
