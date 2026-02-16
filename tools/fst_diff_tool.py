#!/usr/bin/env python3
import argparse
import difflib
import fnmatch
import hashlib
import json
import os
import pickle
import re
import sys
from bisect import bisect_right
from collections import namedtuple
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
BacktraceSummary = Tuple[str, int, Optional[int], Optional[int], str, str, str, str, str, str]


def _die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Diff selected signals between two FST waveforms (explicit signals required by default).")
    parser.add_argument("--fst-a", "--ref", dest="fst_a", required=True,
                        help="Reference FST (e.g. ref run)")
    parser.add_argument("--fst-b", "--dut", "--wolf", dest="fst_b", required=True,
                        help="Target FST (e.g. wolf run)")

    parser.add_argument("--signals", default="",
                        help="Comma-separated signal list (supports glob). Required unless --allow-all-signals.")
    parser.add_argument("--signal", action="append", default=[],
                        help="Signal (repeatable, supports glob). Required unless --allow-all-signals.")
    parser.add_argument("--signals-file", default=None,
                        help="Read signals from file (one per line; '#' comments allowed). Required unless --allow-all-signals.")
    parser.add_argument("--list", action="store_true", help="List available common signals and exit")
    parser.add_argument("--list-filter", default=None,
                        help="Filter --list output (glob or substring)")

    parser.add_argument("--t0", type=int, default=None,
                        help="ROI start time (ticks). Limits comparisons; scan may still read full file.")
    parser.add_argument("--t1", type=int, default=None,
                        help="ROI end time (ticks, inclusive). Limits comparisons; scan may still read full file.")
    parser.add_argument("--max-diffs", type=int, default=None,
                        help="Limit number of diff records (does not reduce scan time)")
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
    parser.add_argument("--allow-all-signals", action="store_true",
                        help="Allow diffing all common signals when none are specified (may be slow)")
    parser.add_argument("--cache-dir", default="build/artifacts/fst_diff_cache",
                        help="Signal index cache directory (empty to disable)")
    parser.add_argument("--no-cache", action="store_true",
                        help="Disable signal index caching")

    parser.add_argument("--clk", default=None, help="Clock signal name for cycle alignment")
    parser.add_argument("--cycle-start", type=int, default=None, help="Cycle start index (inclusive)")
    parser.add_argument("--cycle-end", type=int, default=None, help="Cycle end index (inclusive)")
    parser.add_argument("--cycle-base", type=int, default=1, choices=[0, 1],
                        help="Cycle index base (0 or 1). Default: 1")
    parser.add_argument("--edge", choices=["rising", "falling"], default="rising",
                        help="Clock edge for cycles")

    # Backtrace-only tool: event/counter/fast modes removed.

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


def _cache_key(path: str) -> str:
    abspath = os.path.abspath(path)
    digest = hashlib.sha1(abspath.encode("utf-8")).hexdigest()[:16]
    base = os.path.basename(path)
    return f"{base}.{digest}.pkl"


def _load_cached_signals(path: str, cache_dir: Optional[str]):
    if not cache_dir:
        return None
    cache_path = os.path.join(cache_dir, _cache_key(path))
    if not os.path.exists(cache_path):
        return None
    try:
        with open(cache_path, "rb") as handle:
            data = pickle.load(handle)
    except Exception:
        return None
    if data.get("version") != 1:
        return None
    try:
        stat_size = os.path.getsize(path)
        stat_mtime = os.path.getmtime(path)
    except OSError:
        return None
    if data.get("size") != stat_size or data.get("mtime") != stat_mtime:
        return None
    cached = data.get("signals")
    if not isinstance(cached, dict):
        return None

    Signal = namedtuple("Signal", "name length handle")
    signals_by_name = {}
    signals_by_handle = {}
    for name, payload in cached.items():
        if not isinstance(payload, (list, tuple)) or len(payload) != 2:
            return None
        handle, length = payload
        sig = Signal(name, length, handle)
        signals_by_name[name] = sig
        signals_by_handle.setdefault(handle, sig)
    Signals = namedtuple("Signals", "by_name by_handle")
    return [], Signals(signals_by_name, signals_by_handle)


def _write_cached_signals(path: str, cache_dir: Optional[str], signals) -> None:
    if not cache_dir:
        return
    try:
        os.makedirs(cache_dir, exist_ok=True)
        stat_size = os.path.getsize(path)
        stat_mtime = os.path.getmtime(path)
    except OSError:
        return

    payload = {}
    for name, sig in signals.by_name.items():
        payload[name] = [sig.handle, getattr(sig, "length", None)]

    data = {
        "version": 1,
        "path": os.path.abspath(path),
        "size": stat_size,
        "mtime": stat_mtime,
        "signals": payload,
    }
    cache_path = os.path.join(cache_dir, _cache_key(path))
    try:
        with open(cache_path, "wb") as handle:
            pickle.dump(data, handle, protocol=pickle.HIGHEST_PROTOCOL)
    except OSError:
        return


def _load_signals_cached(ctx, path: str, cache_dir: Optional[str]):
    cached = _load_cached_signals(path, cache_dir)
    if cached is not None:
        return cached
    scopes, signals = pylibfst.get_scopes_signals2(ctx)
    _write_cached_signals(path, cache_dir, signals)
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


def _has_glob(patterns: Iterable[str]) -> bool:
    for pat in patterns:
        if "*" in pat or "?" in pat:
            return True
        if "[" in pat:
            # Ignore trailing bus width suffix like " [63:0]"
            if not _WIDTH_SUFFIX_RE.search(pat):
                return True
    return False


def _resolve_exact_signal(pattern: str, names: Dict[str, object]) -> str:
    if pattern in names:
        return pattern
    hits = [name for name in names.keys()
            if name.endswith("." + pattern) or name.endswith(pattern)]
    if not hits:
        _die(f"signal not found: {pattern}")
    if len(hits) > 1:
        preview = hits[:8]
        more = " ..." if len(hits) > len(preview) else ""
        _die("signal pattern matches multiple signals; please disambiguate:\n  "
             + "\n  ".join(preview) + more)
    return hits[0]


def _resolve_signal_pairs(patterns: List[str],
                          names_a: Dict[str, object],
                          names_b: Dict[str, object]) -> List[Tuple[str, str, str]]:
    resolved: List[Tuple[str, str, str]] = []
    for pat in patterns:
        raw_a = _resolve_exact_signal(pat, names_a)
        raw_b = _resolve_exact_signal(pat, names_b)
        norm = raw_a if raw_a == raw_b else pat
        resolved.append((norm, raw_a, raw_b))
    return resolved


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


class _StopScan(Exception):
    pass


def _collect_events(ctx, handles: List[int], t0: int, t1: Optional[int],
                    need_last_before: bool, stop_after_t1: bool = True
                    ) -> Tuple[Dict[int, List[TimeValue]], Dict[int, Optional[TimeValue]]]:
    events = {h: [] for h in handles}
    last_before: Dict[int, Optional[TimeValue]] = {h: None for h in handles} if need_last_before else {}

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
        if stop_after_t1 and t1 is not None and t > t1:
            raise _StopScan()
        if need_last_before and t <= t0:
            last_before[facidx] = (t, val)
        if t < t0 or (t1 is not None and t > t1):
            return
        events[facidx].append((t, val))

    try:
        pylibfst.fstReaderIterBlocks(ctx, cb)
    except _StopScan:
        pass
    finally:
        if stop_after_t1 and t1 is not None:
            lib.fstReaderSetUnlimitedTimeRange(ctx)
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


def _value_is_one(val: Optional[str]) -> Optional[bool]:
    if val is None:
        return None
    lower = val.lower()
    if "x" in lower or "z" in lower:
        return None
    return "1" in lower


def _clock_edges(ctx, handle: int, t1: Optional[int], edge: str,
                 max_edges: Optional[int] = None) -> List[int]:
    edges: List[int] = []
    prev_level: Optional[bool] = None

    lib.fstReaderClrFacProcessMaskAll(ctx)
    lib.fstReaderSetFacProcessMask(ctx, handle)
    if t1 is not None:
        lib.fstReaderSetLimitTimeRange(ctx, 0, t1)

    def cb(user, time, facidx, value):
        nonlocal prev_level
        t = int(time)
        if t1 is not None and t > t1:
            raise _StopScan()
        val = ffi.string(value).decode("utf-8", errors="replace")
        level = _value_is_one(val)
        if level is None:
            return
        if prev_level is None:
            prev_level = level
            return
        if edge == "rising" and (prev_level is False and level is True):
            edges.append(t)
        elif edge == "falling" and (prev_level is True and level is False):
            edges.append(t)
        prev_level = level
        if max_edges is not None and len(edges) >= max_edges:
            raise _StopScan()

    try:
        pylibfst.fstReaderIterBlocks(ctx, cb)
    except _StopScan:
        pass
    finally:
        if t1 is not None:
            lib.fstReaderSetUnlimitedTimeRange(ctx)
    return edges


def _cycle_index(edges: List[int], time: int, base: int) -> Optional[int]:
    if not edges:
        return None
    idx = bisect_right(edges, time) - 1
    if idx < 0:
        return None
    return idx + base


def _cycle_time(edges: List[int], cycle: int, base: int) -> Optional[int]:
    idx = cycle - base
    if idx < 0 or idx >= len(edges):
        return None
    return edges[idx]


def _get_value_at_time(ctx, handle: int, time: int, buf) -> Optional[str]:
    ptr = lib.fstReaderGetValueFromHandleAtTime(ctx, time, handle, buf)
    if ptr == ffi.NULL:
        return None
    return ffi.string(ptr).decode("utf-8", errors="replace")


def _build_changes(events: List[TimeValue],
                   last_before: Optional[TimeValue],
                   t0: int, t1: int) -> Tuple[Optional[str], Optional[str], List[TimeValue]]:
    start_val: Optional[str] = last_before[1] if last_before is not None else None
    val = start_val
    changes: List[TimeValue] = []
    i = 0
    while i < len(events):
        t = events[i][0]
        if t < t0:
            i += 1
            continue
        if t > t1:
            break
        last_val = events[i][1]
        i += 1
        while i < len(events) and events[i][0] == t:
            last_val = events[i][1]
            i += 1
        if t == t0:
            val = last_val
            start_val = val
            continue
        if last_val != val:
            val = last_val
            changes.append((t, val))
    end_val = val
    return start_val, end_val, changes


def _backtrace_diverge_time(start_a: Optional[str], end_a: Optional[str],
                            changes_a: List[TimeValue],
                            start_b: Optional[str], end_b: Optional[str],
                            changes_b: List[TimeValue],
                            t0: int, ignore_x: bool) -> Tuple[int, str, str]:
    val_a = end_a
    val_b = end_b
    if not _values_differ(val_a, val_b, ignore_x):
        return t0, (val_a if val_a is not None else "x"), (val_b if val_b is not None else "x")
    idx_a = len(changes_a) - 1
    idx_b = len(changes_b) - 1
    while True:
        time_a = changes_a[idx_a][0] if idx_a >= 0 else None
        time_b = changes_b[idx_b][0] if idx_b >= 0 else None
        if time_a is None and time_b is None:
            return t0, (val_a if val_a is not None else "x"), (val_b if val_b is not None else "x")
        if time_b is None or (time_a is not None and time_a >= time_b):
            t = time_a
        else:
            t = time_b
        diff_val_a = val_a
        diff_val_b = val_b
        if time_a == t:
            prev_val = start_a if idx_a == 0 else changes_a[idx_a - 1][1]
            val_a = prev_val
            idx_a -= 1
        if time_b == t:
            prev_val = start_b if idx_b == 0 else changes_b[idx_b - 1][1]
            val_b = prev_val
            idx_b -= 1
        if not _values_differ(val_a, val_b, ignore_x):
            return t, (diff_val_a if diff_val_a is not None else "x"), (diff_val_b if diff_val_b is not None else "x")


def _print_table_backtrace(summaries: List[BacktraceSummary], t0: int, t1: int,
                           matched_count: int, common_count: int,
                           seed_total: int, seed_filtered: int,
                           clk: Optional[str], cycle_base: Optional[int],
                           edge: Optional[str]) -> None:
    header = (f"ROI t=[{t0}, {t1}]  common={common_count}  compared={matched_count}  "
              f"seeds={seed_total}  filtered={seed_filtered}")
    if clk:
        header += f"  clk={clk} edge={edge} cycle_base={cycle_base}"
    print(header)
    if not summaries:
        print("no diff found")
        return
    for (signal, diverge_time, cycle_a, cycle_b,
         val_a, val_b, end_a, end_b, raw_a, raw_b) in summaries:
        print(f"\n{signal}")
        print(f"  diverge_time: {diverge_time}")
        if cycle_a is not None or cycle_b is not None:
            print(f"  cycle_a: {cycle_a}")
            print(f"  cycle_b: {cycle_b}")
        if raw_a != raw_b:
            print(f"  a: {raw_a}")
            print(f"  b: {raw_b}")
        print(f"  value_a: {val_a}")
        print(f"  value_b: {val_b}")
        print(f"  end_value_a: {end_a}")
        print(f"  end_value_b: {end_b}")


def _print_jsonl_backtrace(summaries: List[BacktraceSummary], t0: int, t1: int,
                           matched_count: int, common_count: int,
                           seed_total: int, seed_filtered: int,
                           fst_a: str, fst_b: str, timescale_a: Optional[int],
                           timescale_b: Optional[int], strip_width: bool, ignore_x: bool,
                           include_meta: bool,
                           clk: Optional[str], cycle_base: Optional[int],
                           edge: Optional[str]) -> None:
    if include_meta:
        meta = {
            "_meta": {
                "fst_a": fst_a,
                "fst_b": fst_b,
                "roi_start": t0,
                "roi_end": t1,
                "common_signals": common_count,
                "matched_signals": matched_count,
                "seed_total": seed_total,
                "seed_filtered": seed_filtered,
                "timescale_a": timescale_a,
                "timescale_b": timescale_b,
                "strip_width": strip_width,
                "ignore_x": ignore_x,
                "diff_mode": "backtrace",
                "clk": clk,
                "cycle_base": cycle_base,
                "edge": edge,
            }
        }
        print(json.dumps(meta, ensure_ascii=True, separators=(",", ":")))
    if not summaries:
        print(json.dumps({"result": "no_diff"}, ensure_ascii=True, separators=(",", ":")))
        return
    for rank, (signal, diverge_time, cycle_a, cycle_b,
               val_a, val_b, end_a, end_b, raw_a, raw_b) in enumerate(summaries, start=1):
        rec = {
            "rank": rank,
            "signal": signal,
            "diverge_time": diverge_time,
            "cycle_a": cycle_a,
            "cycle_b": cycle_b,
            "value_a": val_a,
            "value_b": val_b,
            "end_value_a": end_a,
            "end_value_b": end_b,
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
        cache_dir = None
        if not args.no_cache and args.cache_dir:
            cache_dir = args.cache_dir
        _, signals_info_a = _load_signals_cached(ctx_a, args.fst_a, cache_dir)
        _, signals_info_b = _load_signals_cached(ctx_b, args.fst_b, cache_dir)

        names_a = signals_info_a.by_name
        names_b = signals_info_b.by_name

        use_fast_lookup = (
            not args.list
            and not args.allow_all_signals
            and not args.strip_width
            and not _has_glob(signals)
        )

        common: List[str] = []
        map_a: Dict[str, str] = {}
        map_b: Dict[str, str] = {}

        if not use_fast_lookup:
            names_a_list = list(names_a.keys())
            names_b_list = list(names_b.keys())

            map_a, order_a = _build_name_map(names_a_list, args.strip_width)
            map_b, _order_b = _build_name_map(names_b_list, args.strip_width)

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

        if not use_fast_lookup and not common:
            _die("no common signals found between the two FSTs")

        if args.clk:
            if use_fast_lookup:
                raw_clk_a = _resolve_exact_signal(args.clk, names_a)
                raw_clk_b = _resolve_exact_signal(args.clk, names_b)
                clk_norm = raw_clk_a if raw_clk_a == raw_clk_b else args.clk
                map_a[clk_norm] = raw_clk_a
                map_b[clk_norm] = raw_clk_b
            else:
                clk_norm = _match_signals(common, [args.clk])[0]
        else:
            clk_norm = None

        matched_pairs: List[Tuple[str, str, str]] = []
        if signals:
            if use_fast_lookup:
                matched_pairs = _resolve_signal_pairs(signals, names_a, names_b)
                matched = [norm for norm, _raw_a, _raw_b in matched_pairs]
                for norm, raw_a, raw_b in matched_pairs:
                    map_a.setdefault(norm, raw_a)
                    map_b.setdefault(norm, raw_b)
                common = list(matched)
            else:
                matched = _match_signals(common, signals)
        else:
            if args.allow_all_signals:
                matched = common
            else:
                _die("no signals specified; use --signals/--signal/--signals-file")

        if args.max_signals is not None:
            matched = matched[: max(0, args.max_signals)]

        t0 = args.t0 if args.t0 is not None else 0
        end_a = _last_time(ctx_a)
        end_b = _last_time(ctx_b)
        max_end = min(end_a, end_b)
        if args.t1 is None:
            t1 = max_end
        else:
            t1 = min(args.t1, max_end)
        clk_edges_a: List[int] = []
        clk_edges_b: List[int] = []
        if clk_norm is not None:
            clk_handle_a = signals_info_a.by_name[map_a[clk_norm]].handle
            clk_handle_b = signals_info_b.by_name[map_b[clk_norm]].handle
            max_edges = None
            edge_t1 = None
            if args.cycle_start is not None or args.cycle_end is not None:
                max_cycle = args.cycle_end if args.cycle_end is not None else args.cycle_start
                if max_cycle is None:
                    _die("cycle-start/cycle-end require a cycle index")
                max_edges = max_cycle - args.cycle_base + 1
                if max_edges <= 0:
                    _die("cycle-start/cycle-end must be >= cycle-base")
            else:
                edge_t1 = t1 if args.t1 is not None else None
            clk_edges_a = _clock_edges(ctx_a, clk_handle_a, edge_t1, args.edge, max_edges=max_edges)
            clk_edges_b = _clock_edges(ctx_b, clk_handle_b, edge_t1, args.edge, max_edges=max_edges)

        if args.cycle_start is not None:
            if not clk_edges_a or not clk_edges_b:
                _die("cycle-start requires a valid clock signal with edges")
            start_a = _cycle_time(clk_edges_a, args.cycle_start, args.cycle_base)
            start_b = _cycle_time(clk_edges_b, args.cycle_start, args.cycle_base)
            if start_a is None or start_b is None:
                _die("cycle-start is out of range for clock edges")
            t0 = max(t0, start_a, start_b)

        if args.cycle_end is not None:
            if not clk_edges_a or not clk_edges_b:
                _die("cycle-end requires a valid clock signal with edges")
            end_a_time = _cycle_time(clk_edges_a, args.cycle_end, args.cycle_base)
            end_b_time = _cycle_time(clk_edges_b, args.cycle_end, args.cycle_base)
            if end_a_time is None or end_b_time is None:
                _die("cycle-end is out of range for clock edges")
            t1 = min(t1, end_a_time, end_b_time)

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

        backtrace: List[BacktraceSummary] = []
        seed_total = 0
        seed_filtered = 0

        buf_a = ffi.new("char[4096]")
        buf_b = ffi.new("char[4096]")
        seeds = []
        for norm in matched:
            raw_a, raw_b, handle_a, handle_b = signal_map[norm]
            val_a = _get_value_at_time(ctx_a, handle_a, t1, buf_a)
            val_b = _get_value_at_time(ctx_b, handle_b, t1, buf_b)
            if _values_differ(val_a, val_b, args.ignore_x):
                seeds.append((norm, raw_a, raw_b, handle_a, handle_b,
                              val_a if val_a is not None else "x",
                              val_b if val_b is not None else "x"))
        seed_total = len(seeds)

        if seeds:
            handles_seed_a = [seed[3] for seed in seeds]
            handles_seed_b = [seed[4] for seed in seeds]
            events_a, last_before_a = _collect_events(ctx_a, handles_seed_a, t0, t1, need_last_before=True)
            events_b, last_before_b = _collect_events(ctx_b, handles_seed_b, t0, t1, need_last_before=True)

            for norm, raw_a, raw_b, handle_a, handle_b, end_a, end_b in seeds:
                start_a, end_val_a, changes_a = _build_changes(
                    events_a.get(handle_a, []),
                    last_before_a.get(handle_a),
                    t0,
                    t1,
                )
                start_b, end_val_b, changes_b = _build_changes(
                    events_b.get(handle_b, []),
                    last_before_b.get(handle_b),
                    t0,
                    t1,
                )
                if not changes_a and not changes_b:
                    seed_filtered += 1
                    continue
                diverge_time, diff_val_a, diff_val_b = _backtrace_diverge_time(
                    start_a, end_val_a, changes_a,
                    start_b, end_val_b, changes_b,
                    t0, args.ignore_x,
                )
                cycle_a = _cycle_index(clk_edges_a, diverge_time, args.cycle_base) if clk_edges_a else None
                cycle_b = _cycle_index(clk_edges_b, diverge_time, args.cycle_base) if clk_edges_b else None
                backtrace.append((norm, diverge_time, cycle_a, cycle_b,
                                  diff_val_a, diff_val_b,
                                  end_a, end_b, raw_a, raw_b))

            backtrace.sort(key=lambda item: (item[1], item[0]))
            if args.max_diffs is not None:
                backtrace = backtrace[: max(0, args.max_diffs)]

        timescale_a = int(lib.fstReaderGetTimescale(ctx_a))
        timescale_b = int(lib.fstReaderGetTimescale(ctx_b))

        if args.format == "table":
            _print_table_backtrace(backtrace, t0, t1, len(matched), len(common),
                                   seed_total, seed_filtered,
                                   args.clk, args.cycle_base, args.edge)
        else:
            _print_jsonl_backtrace(
                backtrace,
                t0,
                t1,
                len(matched),
                len(common),
                seed_total,
                seed_filtered,
                args.fst_a,
                args.fst_b,
                timescale_a,
                timescale_b,
                args.strip_width,
                args.ignore_x,
                include_meta=not args.no_meta,
                clk=args.clk,
                cycle_base=args.cycle_base,
                edge=args.edge,
            )
    finally:
        _close_fst(ctx_a)
        _close_fst(ctx_b)


if __name__ == "__main__":
    main()
