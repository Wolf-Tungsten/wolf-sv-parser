# stats

`stats` collects structural statistics from a design.

It does not modify the design. Instead, it produces information that is useful for debugging, regression checks, and pipeline inspection.

There are two ways to consume its result:

- as `info` diagnostics
- as a structured session value via `out_stats`

## Python Example

```python
from wolvrix.adapters.stats import StatsValue

with wolvrix.Session() as sess:
    sess.read_sv("top.sv", out_design="design.main")
    diags = sess.run_pass("stats", design="design.main", out_stats="stats.main")
    wolvrix.print_diagnostics(diags, min_level="info")

    stats = sess.get("stats.main")
    assert isinstance(stats, StatsValue)
    stats.write_json("build/stats.json")
```

If you do not provide `out_stats`, the pass still emits diagnostics, but it does not store a separate session value.

## What It Measures

The pass reports counts and distributions such as:

- graph count
- operation count
- value count
- total value bit width
- value width distribution
- operation kind distribution
- register, latch, and memory width distributions
- memory capacity distribution
- write-port cone depth, size, and fan-in distributions
- direct user counts for combinational results
- sink fanout distributions for combinational ops and read ports

## Session Output

Optional output:

- `out_stats`
  - kind `stats`

On the Python side, that session value is adapted into `StatsValue`, which can be serialized or inspected directly.

## Good Use Cases

- compare design size before and after optimization
- catch accidental structural regressions
- dump structured debug information from scripts

## Notes

- this pass does not rewrite IR
- it reports over the whole design, not just the top graph
- diagnostics are good for quick reading
- `out_stats` is better when another tool or script needs structured data
