#!/usr/bin/env python3
"""
Export sparse signal change events from an FST.

Outputs JSONL or CSV with rows: time, signal, value.
"""

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
except Exception as exc:  # pragma: no cover - import guard
    pylibfst = None  # type: ignore
    _IMPORT_ERROR = exc


def _die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def _ensure_pylibfst() -> None:
    if pylibfst is None:
        exe = sys.executable
        hint = ""
        if exe != "/usr/bin/python3" and os.path.exists("/usr/bin/python3"):
            hint = "\ntry: /usr/bin/python3 ..."
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
                hint = "\nclosest matches:\n  " + "\n  ".join(suggestions)
            _die(f"signal not found: {pat}{hint}")
    # De-dup while preserving order
    seen = set()
    uniq = []
    for ref in matched:
        if ref in seen:
            continue
        seen.add(ref)
        uniq.append(ref)
    return uniq


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


def _collect_events(ctx, handles: List[int], t0: int, t1: Optional[int],
                    need_last_before: bool, stop_after_t1: bool = True
                    ) -> Tuple[Dict[int, List[Tuple[int, str]]], Dict[int, Optional[Tuple[int, str]]]]:
    events = {h: [] for h in handles}
    last_before: Dict[int, Optional[Tuple[int, str]]] = {h: None for h in handles} if need_last_before else {}

    lib.fstReaderClrFacProcessMaskAll(ctx)
    for h in handles:
        lib.fstReaderSetFacProcessMask(ctx, h)
    if stop_after_t1 and t1 is not None:
        limit_start = 0 if need_last_before else t0
        lib.fstReaderSetLimitTimeRange(ctx, limit_start, t1)

    def cb(user, time, facidx, value):
        if facidx not in events:
            return
        t = int(time)
        val = ffi.string(value).decode("utf-8", errors="replace")
        if need_last_before and t <= t0:
            last_before[facidx] = (t, val)
        if t < t0 or (t1 is not None and t > t1):
            return
        events[facidx].append((t, val))

    pylibfst.fstReaderIterBlocks(ctx, cb)
    if stop_after_t1 and t1 is not None:
        lib.fstReaderSetUnlimitedTimeRange(ctx)
    return events, last_before


def _parse_signals(args: argparse.Namespace) -> List[str]:
    signals: List[str] = []
    if args.signals:
        for chunk in args.signals.split(","):
            chunk = chunk.strip()
            if chunk:
                signals.append(chunk)
    for sig in args.signal or []:
        if sig:
            signals.append(sig)
    if args.signals_file:
        with open(args.signals_file, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                signals.append(line)
    return signals


def main() -> int:
    parser = argparse.ArgumentParser(description="Export sparse signal change events from an FST")
    parser.add_argument("--fst", required=True, help="Input FST file")
    parser.add_argument("--signal", action="append", default=None, help="Signal (repeatable, supports glob)")
    parser.add_argument("--signals", default=None, help="Comma-separated signal list (supports glob)")
    parser.add_argument("--signals-file", default=None, help="Signals file (one per line, '#' comments allowed)")
    parser.add_argument("--list", action="store_true", help="List available signals and exit")
    parser.add_argument("--list-filter", default=None, help="Filter --list output (glob or substring)")
    parser.add_argument("--t0", type=int, default=0, help="Start time (ticks)")
    parser.add_argument("--t1", type=int, default=None, help="End time (ticks, inclusive)")
    parser.add_argument("--strip-width", action="store_true", help="Strip trailing bus width suffixes")
    parser.add_argument("--include-initial", action="store_true", help="Include last value before t0")
    parser.add_argument("--format", choices=["jsonl", "csv"], default="jsonl")
    parser.add_argument("--max-events", type=int, default=None, help="Limit number of output events")
    args = parser.parse_args()

    ctx = _open_fst(args.fst)
    try:
        _scopes, signals = _load_signals(ctx)
        names = list(signals.by_name.keys())
        mapping, order = _build_name_map(names, args.strip_width)

        if args.list:
            refs = list(mapping.keys())
            if args.list_filter:
                if any(ch in args.list_filter for ch in "*?["):
                    refs = [r for r in refs if fnmatch.fnmatch(r, args.list_filter)]
                else:
                    refs = [r for r in refs if args.list_filter in r]
            for ref in refs:
                print(ref)
            return 0

        patterns = _parse_signals(args)
        if not patterns:
            _die("--signal/--signals/--signals-file required unless --list is used")

        matched_norm = _match_signals(order, patterns)
        matched_raw = [mapping[norm] for norm in matched_norm]

        handles: List[int] = []
        for raw in matched_raw:
            sig = signals.by_name.get(raw)
            if sig is None:
                _die(f"signal not found: {raw}")
            handles.append(sig.handle)

        events, last_before = _collect_events(
            ctx,
            handles,
            t0=args.t0,
            t1=args.t1,
            need_last_before=args.include_initial,
            stop_after_t1=True,
        )

        flat_events: List[Tuple[int, str, str]] = []
        for raw, handle in zip(matched_raw, handles):
            sig_events = list(events.get(handle, []))
            if args.include_initial:
                prev = last_before.get(handle)
                if prev is not None:
                    prev_time, prev_val = prev
                    if prev_time <= args.t0:
                        if not sig_events or sig_events[0][0] > args.t0:
                            sig_events.insert(0, (args.t0, prev_val))
            for t, val in sig_events:
                flat_events.append((t, raw, val))

        flat_events.sort(key=lambda item: (item[0], item[1]))
        if args.max_events is not None:
            flat_events = flat_events[: args.max_events]

        if args.format == "csv":
            print("time,signal,value")
            for t, name, val in flat_events:
                print(f"{t},{name},{val}")
        else:
            for t, name, val in flat_events:
                print(json.dumps({"time": t, "signal": name, "value": val}))
        return 0
    finally:
        _close_fst(ctx)


if __name__ == "__main__":
    raise SystemExit(main())
