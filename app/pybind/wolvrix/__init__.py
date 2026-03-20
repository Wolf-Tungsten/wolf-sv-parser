from __future__ import annotations

import sys

from . import _wolvrix as _native

__all__ = [
    "Design",
    "from_json_string",
    "list_passes",
    "read_json",
    "read_sv",
    "run_pipeline",
]


class Design:
    def __init__(self, capsule):
        self._capsule = capsule

    def run_pass(
        self,
        name: str,
        args: list[str] | None = None,
        dryrun: bool = False,
        diagnostics: str = "warn",
        log_level: str = "warn",
        *,
        print_diagnostics_level: str = "info",
        raise_diagnostics_level: str = "error",
    ) -> tuple[bool, list[dict]]:
        changed, ok, diag = _native.run_pass(
            self._capsule,
            name,
            args or [],
            dryrun,
            diagnostics,
            log_level,
        )
        _print_diagnostics(diag, print_diagnostics_level)
        if _should_raise(diag, raise_diagnostics_level) or (not ok and _should_raise(diag, "error")):
            _raise_with_diagnostics(diag)
        return bool(changed), list(diag)

    def run_pipeline(
        self,
        pipeline: list[str | tuple[str, list[str]] | list],
        dryrun: bool = False,
        diagnostics: str = "warn",
        log_level: str = "warn",
        *,
        print_diagnostics_level: str = "info",
        raise_diagnostics_level: str = "error",
    ) -> tuple[bool, list[dict]]:
        changed, ok, diag = _native.run_pipeline(
            self._capsule,
            pipeline,
            dryrun,
            diagnostics,
            log_level,
        )
        _print_diagnostics(diag, print_diagnostics_level)
        if _should_raise(diag, raise_diagnostics_level) or (not ok and _should_raise(diag, "error")):
            _raise_with_diagnostics(diag)
        return bool(changed), list(diag)

    def write_sv(
        self,
        output: str,
        top: list[str] | None = None,
        split_modules: bool = False,
    ) -> None:
        _native.write_sv(self._capsule, output, top or [], split_modules)

    def to_json(self, mode: str = "pretty-compact", top: list[str] | None = None) -> str:
        return _native.store_json_string(self._capsule, mode, top or [])

    def write_json(self, output: str, mode: str = "pretty-compact", top: list[str] | None = None) -> None:
        text = self.to_json(mode=mode, top=top)
        with open(output, "w", encoding="utf-8") as handle:
            handle.write(text)


def read_sv(
    path: str | None,
    slang_args: list[str] | None = None,
    log_level: str = "info",
    diagnostics: str = "warn",
    *,
    print_diagnostics_level: str = "info",
    raise_diagnostics_level: str = "error",
) -> tuple[Design | None, list[dict]]:
    capsule, ok, diag = _native.read_sv(path, slang_args or [], log_level, diagnostics)
    _print_diagnostics(diag, print_diagnostics_level)
    if _should_raise(diag, raise_diagnostics_level) or (not ok and _should_raise(diag, "error")):
        _raise_with_diagnostics(diag)
    design = Design(capsule) if capsule is not None else None
    return design, list(diag)


def read_json(path: str) -> Design:
    return Design(_native.read_json(path))


def from_json_string(text: str) -> Design:
    return Design(_native.load_json_string(text))


def list_passes() -> list[str]:
    return list(_native.list_passes())


def run_pipeline(
    design: Design,
    pipeline: list[str | tuple[str, list[str]] | list],
    dryrun: bool = False,
    diagnostics: str = "warn",
    log_level: str = "warn",
    *,
    print_diagnostics_level: str = "info",
    raise_diagnostics_level: str = "error",
) -> tuple[bool, list[dict]]:
    return design.run_pipeline(
        pipeline=pipeline,
        dryrun=dryrun,
        diagnostics=diagnostics,
        log_level=log_level,
        print_diagnostics_level=print_diagnostics_level,
        raise_diagnostics_level=raise_diagnostics_level,
    )


def _level_rank(level: str) -> int | None:
    if level is None:
        return None
    name = str(level).lower()
    if name in {"off", "none"}:
        return None
    if name == "warn":
        name = "warning"
    ranks = {
        "debug": 0,
        "info": 1,
        "warning": 2,
        "todo": 3,
        "error": 3,
    }
    return ranks.get(name)


def _print_diagnostics(diags: list[dict], threshold: str) -> None:
    rank = _level_rank(threshold)
    if rank is None:
        return
    for diag in diags or []:
        kind = str(diag.get("kind", "")).lower()
        if kind == "warn":
            kind = "warning"
        kind_rank = _level_rank(kind)
        if kind_rank is None or kind_rank < rank:
            continue
        text = diag.get("text")
        if text:
            print(text, file=sys.stderr, flush=True)


def _format_diagnostics(diags: list[dict]) -> str:
    if not diags:
        return "wolvrix failed"
    parts = [diag.get("text", "") for diag in diags if diag.get("text")]
    return "\n".join(parts) if parts else "wolvrix failed"


def _should_raise(diags: list[dict], threshold: str) -> bool:
    rank = _level_rank(threshold)
    if rank is None:
        return False
    for diag in diags or []:
        kind = str(diag.get("kind", "")).lower()
        if kind == "warn":
            kind = "warning"
        kind_rank = _level_rank(kind)
        if kind_rank is not None and kind_rank >= rank:
            return True
    return False


def _raise_with_diagnostics(diags: list[dict]) -> None:
    exc = RuntimeError(_format_diagnostics(diags))
    setattr(exc, "diagnostics", list(diags))
    raise exc
