#!/usr/bin/env python3
import argparse
import fnmatch
import os
import sys
from typing import Dict, List, Optional, Set, Tuple

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


class Node:
    def __init__(self) -> None:
        self.children: Dict[str, "Node"] = {}
        self.signals: Set[str] = set()


def _die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def _ensure_pylibfst() -> None:
    if pylibfst is None:
        exe = sys.executable
        hint = ""
        if exe != "/usr/bin/python3" and os.path.exists("/usr/bin/python3"):
            hint = "\ntry: /usr/bin/python3 tools/fst_tools/fst_tree.py ..."
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


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print a hierarchical tree of FST scopes/signals."
    )
    parser.add_argument("--fst", required=True, help="Path to .fst file")
    parser.add_argument("--max-depth", type=int, default=None,
                        help="Max scope depth to print")
    parser.add_argument("--show-signals", action="store_true",
                        help="Include signals under each scope")
    parser.add_argument("--filter", default=None,
                        help="Filter by substring or glob (applies to full path)")
    return parser.parse_args()


def _is_glob(pat: str) -> bool:
    return any(c in pat for c in "*?[]")


def _match(pat: Optional[str], path: str) -> bool:
    if not pat:
        return True
    if _is_glob(pat):
        return fnmatch.fnmatch(path, pat)
    return pat in path


def _insert_scope(root: Node, scope: str) -> None:
    cur = root
    if not scope:
        return
    for part in scope.split("."):
        cur = cur.children.setdefault(part, Node())


def _insert_signal(root: Node, full: str) -> None:
    parts = full.split(".")
    if len(parts) == 1:
        root.signals.add(parts[0])
        return
    cur = root
    for part in parts[:-1]:
        cur = cur.children.setdefault(part, Node())
    cur.signals.add(parts[-1])


def _build_tree(scopes: List[str], signals: List[str]) -> Node:
    root = Node()
    for scope in scopes:
        _insert_scope(root, scope)
    for sig in signals:
        _insert_signal(root, sig)
    return root


def _compute_match_flags(root: Node, pat: Optional[str], show_signals: bool) -> Dict[str, bool]:
    match: Dict[str, bool] = {}
    stack: List[Tuple[Node, str, int]] = [(root, "", 0)]
    while stack:
        node, path, state = stack.pop()
        if state == 0:
            stack.append((node, path, 1))
            for name, child in node.children.items():
                child_path = f"{path}.{name}" if path else name
                stack.append((child, child_path, 0))
        else:
            ok = _match(pat, path)
            if show_signals and not ok:
                for s in node.signals:
                    if _match(pat, f"{path}.{s}" if path else s):
                        ok = True
                        break
            if not ok:
                for name, child in node.children.items():
                    child_path = f"{path}.{name}" if path else name
                    if match.get(child_path, False):
                        ok = True
                        break
            match[path] = ok
    return match


def _print_tree(root: Node, max_depth: Optional[int], pat: Optional[str], show_signals: bool) -> None:
    match_flags = _compute_match_flags(root, pat, show_signals)
    stack: List[Tuple[Node, str, int, int]] = [(root, "", 0, 0)]
    while stack:
        node, path, depth, state = stack.pop()
        if max_depth is not None and depth > max_depth:
            continue
        if state == 0:
            stack.append((node, path, depth, 1))
            for name in sorted(node.children.keys(), reverse=True):
                child = node.children[name]
                child_path = f"{path}.{name}" if path else name
                if not match_flags.get(child_path, False):
                    continue
                stack.append((child, child_path, depth + 1, 0))
                print("  " * depth + name)
        else:
            if show_signals:
                for sig in sorted(node.signals):
                    sig_path = f"{path}.{sig}" if path else sig
                    if _match(pat, sig_path):
                        print("  " * depth + sig)


def _print_filtered_path(signals: List[str], path: str, show_signals: bool) -> None:
    parts = [p for p in path.split(".") if p]
    for i, part in enumerate(parts):
        print("  " * i + part)
    if not show_signals:
        return
    prefix = path + "."
    for sig in sorted(signals):
        if not sig.startswith(prefix):
            continue
        rest = sig[len(prefix):]
        if "." not in rest:
            print("  " * len(parts) + rest)


def main() -> int:
    args = _parse_args()
    ctx = _open_fst(args.fst)
    try:
        scopes, signals = _load_signals(ctx)
    finally:
        _close_fst(ctx)

    # pylibfst may return tuples; normalize to list[str]
    scopes_list = [str(s) for s in scopes]
    if hasattr(signals, "by_name"):
        signals_list = [str(s) for s in signals.by_name.keys()]
    else:
        signals_list = [str(s) for s in signals]

    if args.filter and not _is_glob(args.filter):
        pat = args.filter
        scopes_list = [
            s for s in scopes_list
            if (pat in s) or s.startswith(pat) or pat.startswith(s)
        ]
        signals_list = [
            s for s in signals_list
            if s.startswith(pat + ".") or (pat in s) or pat.startswith(s)
        ]

    if args.filter and not _is_glob(args.filter):
        _print_filtered_path(signals_list, args.filter, args.show_signals)
        return 0

    root = _build_tree(scopes_list, signals_list)
    _print_tree(root, args.max_depth, args.filter, args.show_signals)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
