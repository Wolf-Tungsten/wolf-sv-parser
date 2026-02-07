#!/usr/bin/env python3
import argparse
import fnmatch
import json
import os
import sys
from typing import Iterable, List, Optional, Tuple

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


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print a ROI (region of interest) from an FST waveform.")
    parser.add_argument("--fst", required=True, help="Path to .fst file")
    parser.add_argument("--signals", default="", help="Comma-separated signal list (supports glob)")
    parser.add_argument("--signal", action="append", default=[], help="Signal (repeatable, supports glob)")
    parser.add_argument("--list", action="store_true", help="List available signals and exit")

    parser.add_argument("--t0", type=int, default=None, help="ROI start time (ticks)")
    parser.add_argument("--t1", type=int, default=None, help="ROI end time (ticks, inclusive)")

    parser.add_argument("--clk", default=None, help="Clock signal name for cycle-based ROI")
    parser.add_argument("--cycle-start", type=int, default=None, help="Cycle start index (inclusive)")
    parser.add_argument("--cycle-end", type=int, default=None, help="Cycle end index (inclusive)")
    parser.add_argument("--cycle-base", type=int, default=1,
                        help="Cycle index base (0 or 1). Default: 1")
    parser.add_argument("--edge", choices=["rising", "falling"], default="rising",
                        help="Clock edge for cycles")

    parser.add_argument("--format", choices=["table", "csv", "jsonl"], default="jsonl",
                        help="Output format")
    parser.add_argument("--llm", action="store_true",
                        help="LLM-friendly output (alias for --format jsonl)")
    parser.add_argument("--no-meta", action="store_true",
                        help="JSONL: suppress the leading meta line")
    parser.add_argument("--jsonl-mode", choices=["time", "event", "fill"], default="time",
                        help="JSONL: group by time, per-event lines, or filled snapshots")
    parser.add_argument("--include-initial", action="store_true",
                        help="Include last value before t0")
    parser.add_argument("--max-events", type=int, default=None,
                        help="Max events per signal (after filtering)")

    return parser.parse_args()


def _ensure_pylibfst() -> None:
    if pylibfst is None:
        exe = sys.executable
        hint = ""
        if exe != "/usr/bin/python3" and os.path.exists("/usr/bin/python3"):
            hint = "\ntry: /usr/bin/python3 tools/fst_roi/fst_roi.py ..."
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
    scopes, signals = pylibfst.get_scopes_signals2(ctx)
    return list(scopes), signals


def _match_signals(all_refs: List[str], patterns: Iterable[str]) -> List[str]:
    matched: List[str] = []
    for pat in patterns:
        if any(ch in pat for ch in "*?["):
            hits = [ref for ref in all_refs if fnmatch.fnmatch(ref, pat)]
            matched.extend(hits)
            continue
        if pat in all_refs:
            matched.append(pat)
            continue
        # fallback: suffix match
        hits = [ref for ref in all_refs if ref.endswith("." + pat) or ref.endswith(pat)]
        if hits:
            matched.extend(hits)
        else:
            _die(f"signal not found: {pat}")
    # de-dup while preserving order
    seen = set()
    ordered = []
    for ref in matched:
        if ref not in seen:
            seen.add(ref)
            ordered.append(ref)
    return ordered


def _scalar_to_bit(val: str) -> Optional[int]:
    if not val:
        return None
    if val in ("0", "1"):
        return 1 if val == "1" else 0
    if len(val) == 1:
        return None
    # vector: accept if all bits equal
    if all(ch == "0" for ch in val):
        return 0
    if all(ch == "1" for ch in val):
        return 1
    return None


def _find_edges_for_handle(ctx, handle: int, edge: str) -> List[int]:
    edges: List[int] = []
    prev: Optional[int] = None

    lib.fstReaderClrFacProcessMaskAll(ctx)
    lib.fstReaderSetFacProcessMask(ctx, handle)

    def cb(user, time, facidx, value):
        nonlocal prev
        val = ffi.string(value).decode("utf-8", errors="replace")
        b = _scalar_to_bit(val)
        if b is None:
            return
        if prev is None:
            prev = b
            return
        if edge == "rising" and prev == 0 and b == 1:
            edges.append(int(time))
        elif edge == "falling" and prev == 1 and b == 0:
            edges.append(int(time))
        prev = b

    pylibfst.fstReaderIterBlocks(ctx, cb)
    return edges


def _last_time(ctx) -> int:
    return int(lib.fstReaderGetEndTime(ctx))


def _resolve_roi_by_cycle(ctx, clk_handle: int, edge: str,
                          cycle_start: int, cycle_end: int, base: int) -> Tuple[int, Optional[int]]:
    if cycle_end < cycle_start:
        _die("cycle-end must be >= cycle-start")
    if base not in (0, 1):
        _die("cycle-base must be 0 or 1")
    idx0 = cycle_start - base
    idx1 = cycle_end - base
    if idx0 < 0:
        _die("cycle-start is below cycle-base")

    edges = _find_edges_for_handle(ctx, clk_handle, edge)
    if idx0 >= len(edges):
        _die("cycle-start beyond available edges")
    if idx1 >= len(edges):
        _die("cycle-end beyond available edges")

    t0 = edges[idx0]
    if idx1 + 1 < len(edges):
        t1 = edges[idx1 + 1]
    else:
        t1 = None
    return t0, t1


def _timescale_str(ts: int) -> str:
    mapping = {
        0: "1s",
        -3: "1ms",
        -6: "1us",
        -9: "1ns",
        -12: "1ps",
        -15: "1fs",
    }
    return mapping.get(ts, f"1e{ts}s")


def _collect_events(ctx, handles: List[int], t0: int, t1: Optional[int],
                    include_initial: bool, need_last_before: bool) -> Tuple[dict, dict]:
    events = {h: [] for h in handles}
    track_last = include_initial or need_last_before
    last_before = {h: None for h in handles} if track_last else {}

    lib.fstReaderClrFacProcessMaskAll(ctx)
    for h in handles:
        lib.fstReaderSetFacProcessMask(ctx, h)

    def cb(user, time, facidx, value):
        if facidx not in events:
            return
        t = int(time)
        val = ffi.string(value).decode("utf-8", errors="replace")
        if track_last and t <= t0:
            last_before[facidx] = (t, val)
        if t < t0:
            return
        if t1 is not None and t > t1:
            return
        events[facidx].append((t, val))

    pylibfst.fstReaderIterBlocks(ctx, cb)

    if include_initial:
        for h in handles:
            lb = last_before.get(h)
            if lb is None:
                continue
            if not events[h] or events[h][0][0] != lb[0]:
                events[h].insert(0, lb)

    return events, last_before


def _print_table(events_by_handle, handle_to_names, t0: int, t1: Optional[int],
                 max_events: Optional[int], timescale: Optional[int]) -> None:
    header = f"ROI t=[{t0}, {t1 if t1 is not None else 'end'}]"
    if timescale is not None:
        header += f" (timescale {_timescale_str(timescale)})"
    print(header)

    for handle, names in handle_to_names.items():
        tv = events_by_handle.get(handle, [])
        if max_events is not None:
            tv = tv[:max_events]
        for ref in names:
            print(f"\n{ref}")
            if not tv:
                print("  (no events)")
                continue
            for t, v in tv:
                print(f"  {t:>12}  {v}")


def _print_csv(events_by_handle, handle_to_names, max_events: Optional[int]) -> None:
    print("signal,time,value")
    for handle, names in handle_to_names.items():
        tv = events_by_handle.get(handle, [])
        if max_events is not None:
            tv = tv[:max_events]
        for ref in names:
            for t, v in tv:
                print(f"{ref},{t},{v}")


def _print_jsonl(events_by_handle, handle_to_names, t0: int, t1: Optional[int],
                 max_events: Optional[int], timescale: Optional[int], include_meta: bool,
                 mode: str, last_before_by_handle: Optional[dict]) -> None:
    ts = _timescale_str(timescale) if timescale is not None else None
    if include_meta:
        meta = {
            "_meta": {
                "timescale": ts,
                "roi_start": t0,
                "roi_end": t1,
            }
        }
        print(json.dumps(meta, ensure_ascii=True, separators=(",", ":")))
    if mode == "event":
        for handle, names in handle_to_names.items():
            tv = events_by_handle.get(handle, [])
            if max_events is not None:
                tv = tv[:max_events]
            for ref in names:
                for t, v in tv:
                    rec = {"signal": ref, "time": t, "value": v}
                    print(json.dumps(rec, ensure_ascii=True, separators=(",", ":")))
        return

    if mode == "fill":
        time_updates = {}
        for handle, names in handle_to_names.items():
            tv = events_by_handle.get(handle, [])
            if max_events is not None:
                tv = tv[:max_events]
            for t, v in tv:
                bucket = time_updates.get(t)
                if bucket is None:
                    bucket = {}
                    time_updates[t] = bucket
                bucket[handle] = v

        times = sorted(time_updates.keys())
        if not times:
            times = [t0]

        current = {}
        if last_before_by_handle is not None:
            for handle in handle_to_names.keys():
                lb = last_before_by_handle.get(handle)
                if lb is not None:
                    current[handle] = lb[1]

        for t in times:
            updates = time_updates.get(t, {})
            if updates:
                for handle, v in updates.items():
                    current[handle] = v
            values = {}
            for handle, names in handle_to_names.items():
                val = current.get(handle, "x")
                for ref in names:
                    values[ref] = val
            rec = {"time": t, "values": values}
            print(json.dumps(rec, ensure_ascii=True, separators=(",", ":")))
        return

    merged = {}
    for handle, names in handle_to_names.items():
        tv = events_by_handle.get(handle, [])
        if max_events is not None:
            tv = tv[:max_events]
        for t, v in tv:
            bucket = merged.get(t)
            if bucket is None:
                bucket = {}
                merged[t] = bucket
            for ref in names:
                bucket[ref] = v

    for t in sorted(merged.keys()):
        rec = {"time": t, "values": merged[t]}
        print(json.dumps(rec, ensure_ascii=True, separators=(",", ":")))


def main() -> None:
    args = _parse_args()

    signals = []
    if args.signals:
        signals.extend([s.strip() for s in args.signals.split(",") if s.strip()])
    if args.signal:
        signals.extend(args.signal)

    if not os.path.exists(args.fst):
        _die(f"file not found: {args.fst}")
    if args.fst.endswith(".vcd"):
        _die("VCD input is not supported. Please provide an .fst file.")

    if args.llm:
        args.format = "jsonl"

    ctx = _open_fst(args.fst)
    try:
        _, signals_info = _load_signals(ctx)
        all_refs = list(signals_info.by_name.keys())
        if args.list:
            for ref in all_refs:
                print(ref)
            return

        if not signals:
            _die("no signals specified. Use --signals or --signal")

        refs = _match_signals(all_refs, signals)

        if (args.cycle_start is not None or args.cycle_end is not None) and (
            args.t0 is not None or args.t1 is not None
        ):
            _die("use either time-based ROI (--t0/--t1) or cycle-based ROI (--cycle-*)")

        if args.cycle_start is not None or args.cycle_end is not None:
            if args.clk is None:
                _die("--clk is required for cycle-based ROI")
            if args.cycle_start is None or args.cycle_end is None:
                _die("--cycle-start and --cycle-end are both required")
            if args.clk not in signals_info.by_name:
                _die(f"clock not found: {args.clk}")
            clk_handle = signals_info.by_name[args.clk].handle
            _close_fst(ctx)
            ctx = _open_fst(args.fst)
            t0, t1 = _resolve_roi_by_cycle(ctx, clk_handle, args.edge,
                                           args.cycle_start, args.cycle_end, args.cycle_base)
        else:
            t0 = args.t0 if args.t0 is not None else 0
            t1 = args.t1

        if t1 is None:
            t1 = _last_time(ctx)

        handle_to_names = {}
        handles: List[int] = []
        for ref in refs:
            handle = signals_info.by_name[ref].handle
            if handle not in handle_to_names:
                handle_to_names[handle] = []
                handles.append(handle)
            handle_to_names[handle].append(ref)

        events, last_before = _collect_events(ctx, handles, t0, t1, args.include_initial,
                                              need_last_before=(args.jsonl_mode == "fill"))
        timescale = int(lib.fstReaderGetTimescale(ctx))

        if args.format == "table":
            _print_table(events, handle_to_names, t0, t1, args.max_events, timescale)
        elif args.format == "csv":
            _print_csv(events, handle_to_names, args.max_events)
        else:
            _print_jsonl(events, handle_to_names, t0, t1, args.max_events, timescale,
                         include_meta=not args.no_meta, mode=args.jsonl_mode,
                         last_before_by_handle=last_before)
    finally:
        _close_fst(ctx)


if __name__ == "__main__":
    main()
