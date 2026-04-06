# comb-loop-elim

`comb-loop-elim` detects combinational loops and, when possible, repairs false loops.

The important point is that this pass is now diagnostics-first:

- if it fixes a false loop, it reports that as `info`
- if it finds a loop that it cannot safely resolve, it reports that as `warning`
- it only writes a loop report to the session when you explicitly ask for one

## Python Example

```python
with wolvrix.Session() as sess:
    sess.read_sv("top.sv", out_design="design.main")
    diags = sess.run_pass(
        "comb-loop-elim",
        design="design.main",
        out_comb_loop_report="loops.main",
        fix_false_loops=True,
        max_fix_iterations=100,
        fail_on_true_loop=False,
    )
    wolvrix.print_diagnostics(diags, min_level="info")
```

If you omit `out_comb_loop_report`, the pass still returns diagnostics, but it does not silently create a session value.

## What Counts As A Loop

At a high level, the pass:

1. builds a dependency graph over values
2. runs Tarjan SCC detection
3. treats constants, register read ports, latch read ports, and DPI-C calls as boundaries
4. performs a finer-grained bit-range analysis for possible false loops

If the apparent loop is only caused by non-overlapping bit usage, the pass can split values and repair it. That is considered a false loop.

## Session Output

Optional output:

- `out_comb_loop_report`
  - kind `comb-loop.reports`

This is mainly for later inspection or debugging. The primary output of the pass is still diagnostics.

## Key Options

| Option | Default | Meaning |
| --- | --- | --- |
| `-max-analysis-nodes` | `0` | maximum nodes analyzed per graph, `0` means unlimited |
| `-num-threads` | `0` | analysis thread count, `0` uses hardware concurrency |
| `-fix-false-loops` / `-no-fix-false-loops` | true | whether to repair resolvable false loops |
| `-max-fix-iterations` | `100` | maximum repair iterations |
| `-fail-on-true-loop` | false | treat true loops as pass failure |
| `-output-key` | empty | session key for an explicit loop report |

## Diagnostic Raise-Level Interaction

This pass works especially well with the session-level diagnostic raise threshold.

For example:

- with `sess.diagnostics_raise_min_level = "warning"`, any unresolved loop warning becomes an exception
- with `sess.diagnostics_raise_min_level = "error"`, warnings are returned but do not automatically raise

That keeps the pass behavior easy to reason about:

- diagnostics describe what happened
- session raise level decides whether that should stop execution

## Notes

- large designs may need `max_analysis_nodes` tuning
- repairing false loops rewrites the IR
- a true combinational loop usually indicates a real design problem
