#!/usr/bin/env python3
import argparse
import csv
import os
import re
import sys
from typing import Dict, List, Optional, Tuple

try:
    import pylibfst
    from pylibfst import ffi, lib
except Exception as exc:  # pragma: no cover - import guard for missing deps
    pylibfst = None  # type: ignore
    ffi = None  # type: ignore
    lib = None  # type: ignore
    _IMPORT_ERROR = exc
else:
    _IMPORT_ERROR = None


TimeValue = Tuple[int, str]


def _die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def _ensure_pylibfst() -> None:
    if pylibfst is None:
        exe = sys.executable
        hint = ""
        if exe != "/usr/bin/python3" and os.path.exists("/usr/bin/python3"):
            hint = "\ntry: /usr/bin/python3 tools/fst_tools/fst_cycle_trace.py ..."
        _die(
            "missing dependency pylibfst. Install with: pip install pylibfst\n"
            f"python: {exe}\n"
            f"import error: {_IMPORT_ERROR}{hint}"
        )


def _open_fst(path: str):
    _ensure_pylibfst()
    ctx = lib.fstReaderOpen(path.encode())
    if ctx == ffi.NULL:
        _die(f"failed to open fst: {path}")
    return ctx


def _close_fst(ctx) -> None:
    if ctx and ctx != ffi.NULL:
        lib.fstReaderClose(ctx)


def _load_signals(ctx):
    _ensure_pylibfst()
    scopes, signals = pylibfst.get_scopes_signals2(ctx)
    return list(scopes), signals


_WIDTH_SUFFIX_RE = re.compile(r"\s*\[[0-9]+:[0-9]+\]$")


def _strip_width_suffix(name: str) -> str:
    return _WIDTH_SUFFIX_RE.sub("", name)


def _resolve_signals(signals_info, wanted: List[str], match_strip_width: bool) -> Dict[str, object]:
    by_name = signals_info.by_name if hasattr(signals_info, "by_name") else signals_info
    resolved: Dict[str, object] = {}
    if not match_strip_width:
        for name in wanted:
            sig = by_name.get(name)
            if sig is None:
                _die(f"signal not found: {name}")
            resolved[name] = sig
        return resolved

    stripped_map: Dict[str, List[str]] = {}
    for full in by_name.keys():
        key = _strip_width_suffix(full)
        stripped_map.setdefault(key, []).append(full)

    for name in wanted:
        sig = by_name.get(name)
        if sig is not None:
            resolved[name] = sig
            continue
        key = _strip_width_suffix(name)
        matches = stripped_map.get(key, [])
        if not matches:
            _die(f"signal not found: {name}")
        if len(matches) > 1:
            _die(f"ambiguous match for {name}: {matches}")
        resolved[name] = by_name[matches[0]]
    return resolved


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract cycle-based full-signal traces (two lines per cycle)."
    )
    parser.add_argument("--fst", required=True, help="Path to .fst file")
    parser.add_argument("--signals", default="", help="Comma-separated signal list")
    parser.add_argument("--signal", action="append", default=[], help="Signal (repeatable)")
    parser.add_argument("--signals-file", default=None,
                        help="Read signals from file (one per line; '#' comments allowed)")
    parser.add_argument("--match-strip-width", action="store_true",
                        help="Match signals ignoring trailing bus width suffixes")
    parser.add_argument("--clk", required=True, help="Clock signal name")
    parser.add_argument("--cycle-start", type=int, default=None,
                        help="Cycle start index (inclusive)")
    parser.add_argument("--cycle-end", type=int, default=None,
                        help="Cycle end index (inclusive)")
    parser.add_argument("--cycle-base", type=int, default=0,
                        help="Cycle index base (0 or 1). Default: 0")
    parser.add_argument("--out", default=None, help="Output file (default: stdout)")
    return parser.parse_args()


def _load_signal_list(args: argparse.Namespace) -> List[str]:
    signals: List[str] = []
    if args.signals:
        signals.extend([s.strip() for s in args.signals.split(",") if s.strip()])
    if args.signal:
        signals.extend([s.strip() for s in args.signal if s.strip()])
    if args.signals_file:
        with open(args.signals_file, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                signals.append(line)
    if not signals:
        _die("no signals provided; use --signals/--signal/--signals-file")
    return signals


def _collect_edges(ctx, clk_handle: int) -> List[Tuple[int, str]]:
    lib.fstReaderClrFacProcessMaskAll(ctx)
    lib.fstReaderSetFacProcessMask(ctx, clk_handle)

    edges: List[Tuple[int, str]] = []
    prev: Optional[int] = None

    def cb(user, time, facidx, value):
        nonlocal prev
        if facidx != clk_handle:
            return
        b = 1 if ffi.string(value)[:1] == b"1" else 0
        if prev is None:
            prev = b
            return
        if prev == 0 and b == 1:
            edges.append((int(time), "rise"))
        elif prev == 1 and b == 0:
            edges.append((int(time), "fall"))
        prev = b

    pylibfst.fstReaderIterBlocks(ctx, cb)
    return edges


def _collect_events(ctx, handles: List[int], t0: int, t1: int) -> Tuple[Dict[int, List[TimeValue]], Dict[int, Optional[TimeValue]]]:
    events: Dict[int, List[TimeValue]] = {h: [] for h in handles}
    last_before: Dict[int, Optional[TimeValue]] = {h: None for h in handles}

    lib.fstReaderClrFacProcessMaskAll(ctx)
    for h in handles:
        lib.fstReaderSetFacProcessMask(ctx, h)

    def cb(user, time, facidx, value):
        if facidx not in events:
            return
        t = int(time)
        val = ffi.string(value).decode("utf-8", errors="replace")
        if t <= t0:
            last_before[facidx] = (t, val)
        if t < t0 or t > t1:
            return
        events[facidx].append((t, val))

    pylibfst.fstReaderIterBlocks(ctx, cb)
    return events, last_before


def main() -> int:
    args = _parse_args()

    if args.cycle_base not in (0, 1):
        _die("cycle-base must be 0 or 1")

    signals = _load_signal_list(args)
    ctx = _open_fst(args.fst)
    try:
        _, sigs = _load_signals(ctx)
        resolved = _resolve_signals(sigs, signals + [args.clk], args.match_strip_width)
        clk_sig = resolved.get(args.clk)
        if clk_sig is None:
            _die(f"clk not found: {args.clk}")

        signal_order: List[str] = []
        handles: List[int] = []
        for name in signals:
            sig = resolved.get(name)
            if sig is None:
                _die(f"signal not found: {name}")
            signal_order.append(sig.name)
            handles.append(sig.handle)

        edges = _collect_edges(ctx, clk_sig.handle)
        if not edges:
            _die("no clock edges found")

        samples: List[Tuple[int, int, str]] = []
        cycle = -1
        for t, phase in edges:
            if phase == "rise":
                cycle += 1
                samples.append((t, cycle, "rise"))
            else:
                if cycle >= 0:
                    samples.append((t, cycle, "fall"))

        def in_range(cyc: int) -> bool:
            user_cyc = cyc + args.cycle_base
            if args.cycle_start is not None and user_cyc < args.cycle_start:
                return False
            if args.cycle_end is not None and user_cyc > args.cycle_end:
                return False
            return True

        samples = [s for s in samples if in_range(s[1])]
        if not samples:
            _die("no samples after applying cycle range")

        t0 = min(t for t, _, _ in samples)
        t1 = max(t for t, _, _ in samples)

        events, last_before = _collect_events(ctx, handles, t0, t1)

        # Initialize state from last_before.
        state: Dict[int, str] = {}
        for h in handles:
            lb = last_before.get(h)
            if lb is not None:
                state[h] = lb[1]
        idx_map: Dict[int, int] = {h: 0 for h in handles}

        out = open(args.out, "w", encoding="utf-8", newline="") if args.out else sys.stdout
        try:
            writer = csv.writer(out)
            header = ["time", "cycle", "phase"] + signal_order
            writer.writerow(header)
            for t, cyc, phase in samples:
                # Advance each signal to time t.
                for h in handles:
                    tv = events.get(h, [])
                    idx = idx_map[h]
                    while idx < len(tv) and tv[idx][0] <= t:
                        state[h] = tv[idx][1]
                        idx += 1
                    idx_map[h] = idx

                row = [t, cyc + args.cycle_base, phase]
                row.extend(state.get(h, "x") for h in handles)
                writer.writerow(row)
        finally:
            if args.out:
                out.close()
    finally:
        _close_fst(ctx)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
