#!/usr/bin/env python3
import argparse
import difflib
import fnmatch
import json
import os
import re
import sys
from typing import Dict, Iterable, List, Optional, Tuple

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
DiffRecord = Tuple[int, str, str, str, str, str]


def _die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Find the earliest differing signal between two FST waveforms.")
    parser.add_argument("--fst-a", "--ref", dest="fst_a", required=True,
                        help="Reference FST (e.g. ref run)")
    parser.add_argument("--fst-b", "--dut", "--wolf", dest="fst_b", required=True,
                        help="Target FST (e.g. wolf run)")

    parser.add_argument("--signals", default="", help="Comma-separated signal list (supports glob)")
    parser.add_argument("--signal", action="append", default=[], help="Signal (repeatable, supports glob)")
    parser.add_argument("--signals-file", default=None,
                        help="Read signals from file (one per line; '#' comments allowed)")
    parser.add_argument("--list", action="store_true", help="List available common signals and exit")
    parser.add_argument("--list-filter", default=None,
                        help="Filter --list output (glob or substring)")

    parser.add_argument("--t0", type=int, default=None, help="ROI start time (ticks)")
    parser.add_argument("--t1", type=int, default=None, help="ROI end time (ticks, inclusive)")
    parser.add_argument("--top", type=int, default=1, help="Number of earliest diffs to print")
    parser.add_argument("--max-signals", type=int, default=None,
                        help="Limit number of matched signals (after filtering)")

    parser.add_argument("--format", choices=["jsonl", "table"], default="jsonl",
                        help="Output format")
    parser.add_argument("--llm", action="store_true",
                        help="LLM-friendly output (alias for --format jsonl)")
    parser.add_argument("--no-meta", action="store_true",
                        help="JSONL: suppress the leading meta line")
    parser.add_argument("--strip-width", action="store_true",
                        help="Strip trailing bus width suffixes (e.g. ' [31:0]')")
    parser.add_argument("--ignore-x", action="store_true",
                        help="Ignore diffs when either value is X/Z/unknown")
    parser.add_argument("--no-initial", action="store_true",
                        help="Do not use the last value before t0 as initial state")

    return parser.parse_args()


def _ensure_pylibfst() -> None:
    if pylibfst is None:
        exe = sys.executable
        hint = ""
        if exe != "/usr/bin/python3" and os.path.exists("/usr/bin/python3"):
            hint = "\ntry: /usr/bin/python3 tools/fst_diff_tool.py ..."
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


_WIDTH_SUFFIX_RE = re.compile(r"\s*\[[0-9]+:[0-9]+\]$")


def _strip_width_suffix(name: str) -> str:
    return _WIDTH_SUFFIX_RE.sub("", name)


def _best_matches(all_refs: List[str], pat: str, limit: int = 8) -> List[str]:
    close = difflib.get_close_matches(pat, all_refs, n=limit, cutoff=0.2)
    if close:
        return close
    suffix_hits = [ref for ref in all_refs if ref.endswith("." + pat) or ref.endswith(pat)]
    return suffix_hits[:limit]


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
        hits = [ref for ref in all_refs if ref.endswith("." + pat) or ref.endswith(pat)]
        if hits:
            matched.extend(hits)
        else:
            suggestions = _best_matches(all_refs, pat)
            hint = ""
            if suggestions:
                hint = "\n  close matches:\n    " + "\n    ".join(suggestions)
            _die(f"signal not found: {pat}{hint}")
    seen = set()
    ordered = []
    for ref in matched:
        if ref not in seen:
            seen.add(ref)
            ordered.append(ref)
    return ordered


def _build_name_map(names: List[str], strip_width: bool) -> Tuple[Dict[str, str], List[str]]:
    if not strip_width:
        mapping = {name: name for name in names}
        return mapping, list(names)

    mapping: Dict[str, str] = {}
    order: List[str] = []
    collisions: Dict[str, List[str]] = {}
    for raw in names:
        norm = _strip_width_suffix(raw)
        if norm in mapping:
            collisions.setdefault(norm, []).extend([mapping[norm], raw])
            continue
        mapping[norm] = raw
        order.append(norm)

    if collisions:
        details = []
        for norm, raws in collisions.items():
            uniq = []
            for r in raws:
                if r not in uniq:
                    uniq.append(r)
            details.append(f"{norm}: {', '.join(uniq)}")
        _die("strip-width collisions detected:\n  " + "\n  ".join(details))

    return mapping, order


def _collect_events(ctx, handles: List[int], t0: int, t1: int,
                    need_last_before: bool) -> Tuple[Dict[int, List[TimeValue]], Dict[int, Optional[TimeValue]]]:
    events = {h: [] for h in handles}
    last_before: Dict[int, Optional[TimeValue]] = {h: None for h in handles} if need_last_before else {}

    lib.fstReaderClrFacProcessMaskAll(ctx)
    for h in handles:
        lib.fstReaderSetFacProcessMask(ctx, h)

    def cb(user, time, facidx, value):
        if facidx not in events:
            return
        t = int(time)
        val = ffi.string(value).decode("utf-8", errors="replace")
        if need_last_before and t <= t0:
            last_before[facidx] = (t, val)
        if t < t0 or t > t1:
            return
        events[facidx].append((t, val))

    pylibfst.fstReaderIterBlocks(ctx, cb)
    return events, last_before


def _last_time(ctx) -> int:
    return int(lib.fstReaderGetEndTime(ctx))


def _is_unknown(val: Optional[str]) -> bool:
    if val is None:
        return True
    lower = val.lower()
    return "x" in lower or "z" in lower


def _values_differ(a: Optional[str], b: Optional[str], ignore_x: bool) -> bool:
    if ignore_x and (_is_unknown(a) or _is_unknown(b)):
        return False
    av = a if a is not None else "x"
    bv = b if b is not None else "x"
    return av != bv


def _first_diff(events_a: List[TimeValue], events_b: List[TimeValue],
                last_a: Optional[TimeValue], last_b: Optional[TimeValue],
                t0: int, t1: int, include_initial: bool,
                ignore_x: bool) -> Optional[Tuple[int, str, str]]:
    i = 0
    j = 0
    val_a: Optional[str] = None
    val_b: Optional[str] = None
    if include_initial:
        if last_a is not None:
            val_a = last_a[1]
        if last_b is not None:
            val_b = last_b[1]

    while i < len(events_a) and events_a[i][0] == t0:
        val_a = events_a[i][1]
        i += 1
    while j < len(events_b) and events_b[j][0] == t0:
        val_b = events_b[j][1]
        j += 1

    if _values_differ(val_a, val_b, ignore_x):
        return t0, (val_a if val_a is not None else "x"), (val_b if val_b is not None else "x")

    while i < len(events_a) or j < len(events_b):
        next_a = events_a[i][0] if i < len(events_a) else None
        next_b = events_b[j][0] if j < len(events_b) else None
        if next_a is None:
            next_t = next_b
        elif next_b is None:
            next_t = next_a
        else:
            next_t = next_a if next_a <= next_b else next_b
        if next_t is None or next_t > t1:
            break
        while i < len(events_a) and events_a[i][0] == next_t:
            val_a = events_a[i][1]
            i += 1
        while j < len(events_b) and events_b[j][0] == next_t:
            val_b = events_b[j][1]
            j += 1
        if _values_differ(val_a, val_b, ignore_x):
            return next_t, (val_a if val_a is not None else "x"), (val_b if val_b is not None else "x")
    return None


def _print_table(diffs: List[DiffRecord], t0: int, t1: int,
                 matched_count: int, common_count: int) -> None:
    header = f"ROI t=[{t0}, {t1}]  common={common_count}  compared={matched_count}"
    print(header)
    if not diffs:
        print("no diff found")
        return
    for time, signal, val_a, val_b, raw_a, raw_b in diffs:
        print(f"\n{time}  {signal}")
        if raw_a != raw_b:
            print(f"  a: {raw_a}")
            print(f"  b: {raw_b}")
        print(f"  value_a: {val_a}")
        print(f"  value_b: {val_b}")


def _print_jsonl(diffs: List[DiffRecord], t0: int, t1: int,
                 matched_count: int, common_count: int,
                 fst_a: str, fst_b: str, timescale_a: Optional[int],
                 timescale_b: Optional[int], strip_width: bool, ignore_x: bool,
                 include_meta: bool) -> None:
    if include_meta:
        meta = {
            "_meta": {
                "fst_a": fst_a,
                "fst_b": fst_b,
                "roi_start": t0,
                "roi_end": t1,
                "common_signals": common_count,
                "matched_signals": matched_count,
                "timescale_a": timescale_a,
                "timescale_b": timescale_b,
                "strip_width": strip_width,
                "ignore_x": ignore_x,
            }
        }
        print(json.dumps(meta, ensure_ascii=True, separators=(",", ":")))
    if not diffs:
        print(json.dumps({"result": "no_diff"}, ensure_ascii=True, separators=(",", ":")))
        return
    for rank, (time, signal, val_a, val_b, raw_a, raw_b) in enumerate(diffs, start=1):
        rec = {
            "rank": rank,
            "time": time,
            "signal": signal,
            "value_a": val_a,
            "value_b": val_b,
            "signal_a": raw_a,
            "signal_b": raw_b,
        }
        print(json.dumps(rec, ensure_ascii=True, separators=(",", ":")))


def main() -> None:
    args = _parse_args()

    signals = []
    if args.signals:
        signals.extend([s.strip() for s in args.signals.split(",") if s.strip()])
    if args.signal:
        signals.extend(args.signal)
    if args.signals_file:
        if not os.path.exists(args.signals_file):
            _die(f"signals file not found: {args.signals_file}")
        with open(args.signals_file, "r", encoding="utf-8") as handle:
            for line in handle:
                raw = line.strip()
                if not raw or raw.startswith("#"):
                    continue
                signals.append(raw)

    if args.llm:
        args.format = "jsonl"

    for path in (args.fst_a, args.fst_b):
        if not os.path.exists(path):
            _die(f"file not found: {path}")
        if path.endswith(".vcd"):
            _die("VCD input is not supported. Please provide an .fst file.")

    ctx_a = _open_fst(args.fst_a)
    ctx_b = _open_fst(args.fst_b)
    try:
        _, signals_info_a = _load_signals(ctx_a)
        _, signals_info_b = _load_signals(ctx_b)
        names_a = list(signals_info_a.by_name.keys())
        names_b = list(signals_info_b.by_name.keys())

        map_a, order_a = _build_name_map(names_a, args.strip_width)
        map_b, _order_b = _build_name_map(names_b, args.strip_width)

        common_set = set(map_a.keys()) & set(map_b.keys())
        common = [name for name in order_a if name in common_set]

        if args.list:
            refs = common
            if args.list_filter:
                if any(ch in args.list_filter for ch in "*?["):
                    refs = [r for r in refs if fnmatch.fnmatch(r, args.list_filter)]
                else:
                    refs = [r for r in refs if args.list_filter in r]
            for ref in refs:
                print(ref)
            return

        if not common:
            _die("no common signals found between the two FSTs")

        if signals:
            matched = _match_signals(common, signals)
        else:
            matched = common

        if args.max_signals is not None:
            matched = matched[: max(0, args.max_signals)]

        if not matched:
            _die("no signals matched after filtering")

        t0 = args.t0 if args.t0 is not None else 0
        end_a = _last_time(ctx_a)
        end_b = _last_time(ctx_b)
        max_end = min(end_a, end_b)
        if args.t1 is None:
            t1 = max_end
        else:
            t1 = min(args.t1, max_end)
        if t1 < t0:
            _die("t1 must be >= t0 within the common time range")

        handles_a: List[int] = []
        handles_b: List[int] = []
        signal_map: Dict[str, Tuple[str, str, int, int]] = {}
        for norm in matched:
            raw_a = map_a[norm]
            raw_b = map_b[norm]
            handle_a = signals_info_a.by_name[raw_a].handle
            handle_b = signals_info_b.by_name[raw_b].handle
            handles_a.append(handle_a)
            handles_b.append(handle_b)
            signal_map[norm] = (raw_a, raw_b, handle_a, handle_b)

        events_a, last_before_a = _collect_events(ctx_a, handles_a, t0, t1, need_last_before=True)
        events_b, last_before_b = _collect_events(ctx_b, handles_b, t0, t1, need_last_before=True)

        diffs: List[DiffRecord] = []
        include_initial = not args.no_initial
        for norm in matched:
            raw_a, raw_b, handle_a, handle_b = signal_map[norm]
            diff = _first_diff(
                events_a.get(handle_a, []),
                events_b.get(handle_b, []),
                last_before_a.get(handle_a),
                last_before_b.get(handle_b),
                t0,
                t1,
                include_initial,
                args.ignore_x,
            )
            if diff is None:
                continue
            time, val_a, val_b = diff
            diffs.append((time, norm, val_a, val_b, raw_a, raw_b))

        diffs.sort(key=lambda item: (item[0], item[1]))
        if args.top is not None:
            if args.top <= 0:
                diffs = []
            else:
                diffs = diffs[: args.top]

        timescale_a = int(lib.fstReaderGetTimescale(ctx_a))
        timescale_b = int(lib.fstReaderGetTimescale(ctx_b))

        if args.format == "table":
            _print_table(diffs, t0, t1, len(matched), len(common))
        else:
            _print_jsonl(
                diffs,
                t0,
                t1,
                len(matched),
                len(common),
                args.fst_a,
                args.fst_b,
                timescale_a,
                timescale_b,
                args.strip_width,
                args.ignore_x,
                include_meta=not args.no_meta,
            )
    finally:
        _close_fst(ctx_a)
        _close_fst(ctx_b)


if __name__ == "__main__":
    main()
