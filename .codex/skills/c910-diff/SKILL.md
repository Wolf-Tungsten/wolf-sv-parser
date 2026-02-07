---
name: c910-diff
description: Compare the latest C910 coremark ref vs wolf logs, decide whether outputs are equivalent, and, if not, use waveform diffing (no extra reruns) to isolate the smallest mismatching module. Use when investigating mismatches between build/logs/c910/c910_ref_coremark_*.log and build/logs/c910/c910_wolf_coremark_*.log or when run_c910_diff results are not equivalent.
---

# C910 Diff

## Overview

Guide a repeatable diff workflow for C910 coremark runs: always start by generating fresh logs and waveforms, judge equivalence, and, if mismatched, use the FST ROI tool to isolate the smallest root-cause module without extra reruns or RTL instrumentation. Many modules may show downstream differences; the explicit goal is the earliest/root-cause module that originates the mismatch. Maintain a running report under docs/c910 and converge to the smallest root-cause module.

## Workflow

### Inputs

- `max_cycles` (required): maximum simulation cycles to run. Use it to set `C910_SIM_MAX_CYCLE`.

### 0) Always start with a fresh run (logs + waveforms)

From repo root, run this first on every investigation:

```bash
C910_SIM_MAX_CYCLE=<max_cycles> C910_WAVEFORM=1 make run_c910_diff -j
```

This ensures log paths and instrumentation reflect the current state.

### 1) Collect the latest logs and waveforms

- Find the newest ref and wolf logs by mtime:

```bash
ls -t build/logs/c910/c910_ref_coremark_*.log | head -1
ls -t build/logs/c910/c910_wolf_coremark_*.log | head -1
```

- Find the newest ref and wolf waveforms (default: same directory as logs):

```bash
ls -t build/logs/c910/c910_ref_coremark_*.fst | head -1
ls -t build/logs/c910/c910_wolf_coremark_*.fst | head -1
```

Record the log and FST paths in the report (they should be from the fresh run in step 0).

### 2) Decide equivalence

- Scan for errors or warnings first:

```bash
rg -n "ERROR|FATAL|ASSERT|WARNING" <ref_log>
rg -n "ERROR|FATAL|ASSERT|WARNING" <wolf_log>
```

- Compare key CoreMark output lines (cycles, iterations, score, size):

```bash
rg -n "VCUNT_SIM|CoreMark" <ref_log>
rg -n "VCUNT_SIM|CoreMark" <wolf_log>
```

- Run a full diff. If needed, filter only known nondeterministic lines (timestamps, absolute paths, build metadata) and document any filters used:

```bash
diff -u <ref_log> <wolf_log>
```

Treat the logs as equivalent only when functional output matches (CoreMark results and error/warn presence) and any remaining differences are clearly nondeterministic.

### 3) If equivalent, document and stop

Record the equivalence decision and evidence in the report, then stop. Do not invent further work.

### 4) If not equivalent, use waveforms to isolate the smallest mismatching module

Use C910 hierarchy knowledge to pick signal groups and compare ref vs wolf waveforms, narrowing scope step-by-step without rerunning simulation. Many modules may appear different due to cascading effects; always push toward the earliest divergence and identify the root-cause module that originates the mismatch. Examples of likely high-impact areas:

- Reset, clock gating, or power-up sequencing differences
- CSR or interrupt behavior (pending/enable/ack)
- Cache or memory ordering effects
- Uninitialized or X-propagated signals
- Width/sign mismatches or truncation during emit

Keep hypotheses short and traceable to candidate modules/signals. Iterate by narrowing from top-level signals to submodule signals until the smallest mismatching module is identified.

Recommended waveform workflow (no extra runs):

1) Identify a small set of top-level signals to compare (clock/reset/PC/commit/CSR/memory interface).
2) Use the FST ROI tool to extract a short time window around the first divergence.
3) Compare ref vs wolf JSONL output; when you see a differing signal, descend into that module’s sub-hierarchy and repeat.
4) Continue until the smallest root-cause module is isolated (the earliest module where the mismatch originates).

Root-cause module judgment criteria (use multiple signals to confirm):

1) **Earliest divergence time**: the root-cause module shows the first timepoint where ref/wolf values differ; downstream modules diverge at the same time or later.
2) **Upstream inputs match, internal/output diverges**: for the candidate module, inputs remain identical between ref/wolf while an internal node or output diverges. If inputs already differ, move one level upstream.
3) **Clock/reset/enable alignment**: rule out trivial differences due to reset release, clock gating, or enable signals. If those differ earlier, they are the root-cause candidate.
4) **Stateful vs combinational**: a stateful element (flop/CSR/cache tag) whose next state diverges while combinational inputs match is a strong root-cause signal.
5) **Single-source fanout**: if a single output from module A feeds multiple downstream modules and all downstream differences can be explained by that output divergence, module A is the likely root cause.
6) **Minimal scope confirmation**: once a candidate module is found, confirm by checking a smaller sub-hierarchy inside it; if no earlier divergence is found, finalize the module.

Waveform tool:

```bash
python3 tools/fst_roi/fst_roi.py --fst <waveform.fst> --signals <sigA>,<sigB> --t0 <start> --t1 <end> --jsonl-mode fill
```

Prefer `--jsonl-mode fill` to get full per-time snapshots. Use `--jsonl-mode time` for lighter output and `--jsonl-mode event` for raw event streams.

### 5) Analyze and iterate

- Compare ref vs wolf ROI output and narrow the scope.
- Repeat waveform ROI extraction until the root cause module is identified.
- Each iteration should reduce output volume and tighten scope, progressively zooming in on the failing block or signal.
- Allow multiple phase-level notes per iteration (e.g., "phase: log triage", "phase: top-level diff", "phase: narrowed to module X").

## Report requirements

Write a running report at docs/c910/c910_diff_report.md with an entry per iteration, and allow multiple phase notes per iteration/run. Each entry should include:

- Timestamp (YYYY-MM-DD HH:MM) and command used
- Log paths and FST paths for ref and wolf
- Equivalence decision and evidence (key diff lines or summaries)
- Hypotheses for the next iteration
- Waveform ROI extraction details (signals, time window, jsonl mode)
- Results and next steps (include a phase label when the iteration has multiple stages)

Stop once equivalence is confirmed or the root cause is identified and documented. The final report must name the smallest root-cause module and explicitly tell the user that convergence point, noting that downstream modules may also differ but are not the origin.
