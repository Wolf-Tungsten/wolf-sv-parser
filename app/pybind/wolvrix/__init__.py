from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from . import _wolvrix as _native
from .adapters import adapt_session_value

__all__ = [
    "OpaqueValue",
    "Session",
    "list_passes",
]


@dataclass(frozen=True)
class OpaqueValue:
    key: str
    kind: str

    def __repr__(self) -> str:
        return f"OpaqueValue(key={self.key!r}, kind={self.kind!r})"


class Session:
    def __init__(self) -> None:
        self._capsule = _native.create_session()
        self._diagnostics_policy = "error"
        self._log_level = "warn"
        self._history: list[dict[str, Any]] = []

    def __enter__(self) -> "Session":
        self._ensure_open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def close(self) -> None:
        if self._capsule is None:
            return
        _native.session_close(self._capsule)
        self._capsule = None

    def __contains__(self, key: str) -> bool:
        self._ensure_open()
        return bool(_native.session_contains(self._capsule, key))

    def keys(self, prefix: str | None = None, kind: str | None = None) -> list[str]:
        self._ensure_open()
        return list(_native.session_keys(self._capsule, prefix=prefix, kind=kind))

    def kind(self, key: str) -> str:
        self._ensure_open()
        return str(_native.session_kind(self._capsule, key))

    def get(self, key: str):
        self._ensure_open()
        storage, kind, payload = _native.session_get_value(self._capsule, key)
        adapted = adapt_session_value(self, key=key, storage=storage, kind=kind, payload=payload)
        if adapted is not None:
            return adapted
        if storage == "python":
            return payload
        return OpaqueValue(key=key, kind=kind)

    def put(self, key: str, value, *, kind: str, replace: bool = False) -> None:
        self._ensure_open()
        if isinstance(value, OpaqueValue):
            raise TypeError("opaque session values cannot be re-put from Python; use copy/rename instead")
        _native.session_put_python(self._capsule, key=key, value=value, kind=kind, replace=replace)

    def delete(self, key: str) -> None:
        self._ensure_open()
        _native.session_delete(self._capsule, key)

    def rename(self, src: str, dst: str, *, replace: bool = False) -> None:
        self._ensure_open()
        _native.session_rename(self._capsule, src=src, dst=dst, replace=replace)

    def copy(self, src: str, dst: str, *, replace: bool = False) -> None:
        self._ensure_open()
        _native.session_copy(self._capsule, src=src, dst=dst, replace=replace)

    def set_diagnostics_policy(self, level: str) -> None:
        self._diagnostics_policy = _normalize_diagnostics_policy(level)

    def diagnostics_policy(self) -> str:
        return self._diagnostics_policy

    def set_log_level(self, level: str) -> None:
        self._log_level = _normalize_log_level(level)

    def log_level(self) -> str:
        return self._log_level

    def history(self) -> list[dict]:
        return [dict(item) for item in self._history]

    def read_sv(
        self,
        path: str | None,
        *,
        target_design_key: str = "design.main",
        slang_args: list[str] | None = None,
        replace: bool = False,
    ) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_read_sv(
            self._capsule,
            path=path,
            target_design_key=target_design_key,
            slang_args=slang_args or [],
            replace=replace,
            log_level=self._log_level,
        )
        return self._complete_action(
            "read_sv",
            diagnostics,
            success=bool(success),
            target_design_key=target_design_key,
            path=path,
        )

    def read_json_file(
        self,
        path: str,
        *,
        target_design_key: str = "design.main",
        replace: bool = False,
    ) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_read_json_file(
            self._capsule,
            path=path,
            target_design_key=target_design_key,
            replace=replace,
        )
        return self._complete_action(
            "read_json_file",
            diagnostics,
            success=bool(success),
            target_design_key=target_design_key,
            path=path,
        )

    def load_json_text(
        self,
        text: str,
        *,
        target_design_key: str = "design.main",
        replace: bool = False,
    ) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_load_json_text(
            self._capsule,
            text=text,
            target_design_key=target_design_key,
            replace=replace,
        )
        return self._complete_action(
            "load_json_text",
            diagnostics,
            success=bool(success),
            target_design_key=target_design_key,
        )

    def clone_design(self, src: str, dst: str, *, replace: bool = False) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_clone_design(
            self._capsule,
            src=src,
            dst=dst,
            replace=replace,
        )
        return self._complete_action(
            "clone_design",
            diagnostics,
            success=bool(success),
            src=src,
            dst=dst,
        )

    def run_pass(
        self,
        name: str,
        *,
        design: str,
        args: list[str] | None = None,
        dryrun: bool = False,
        **named_session_keys,
    ) -> list[dict]:
        self._ensure_open()
        canonical_name, pass_args = _compile_run_pass(name, args or [], named_session_keys)
        success, changed, diagnostics = _native.session_run_pass(
            self._capsule,
            name=canonical_name,
            design=design,
            args=pass_args,
            dryrun=dryrun,
            log_level=self._log_level,
        )
        return self._complete_action(
            "run_pass",
            diagnostics,
            success=bool(success),
            changed=bool(changed),
            name=canonical_name,
            design=design,
            dryrun=dryrun,
        )

    def store_json(
        self,
        *,
        design: str,
        output: str,
        mode: str = "pretty-compact",
        top: list[str] | None = None,
    ) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_store_json(
            self._capsule,
            design=design,
            output=output,
            mode=mode,
            top=top or [],
        )
        return self._complete_action(
            "store_json",
            diagnostics,
            success=bool(success),
            design=design,
            output=output,
        )

    def emit_sv(
        self,
        *,
        design: str,
        output: str,
        top: list[str] | None = None,
        split_modules: bool = False,
    ) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_emit_sv(
            self._capsule,
            design=design,
            output=output,
            top=top or [],
            split_modules=split_modules,
        )
        return self._complete_action(
            "emit_sv",
            diagnostics,
            success=bool(success),
            design=design,
            output=output,
        )

    def emit_verilator_repcut_package(
        self,
        *,
        design: str,
        output: str,
        top: list[str] | None = None,
    ) -> list[dict]:
        self._ensure_open()
        success, diagnostics = _native.session_emit_verilator_repcut_package(
            self._capsule,
            design=design,
            output=output,
            top=top or [],
        )
        return self._complete_action(
            "emit_verilator_repcut_package",
            diagnostics,
            success=bool(success),
            design=design,
            output=output,
        )

    def _complete_action(self, action: str, diagnostics: list[dict], *, success: bool, **details) -> list[dict]:
        result = list(diagnostics or [])
        entry = {"action": action, "success": bool(success), "diagnostics": result}
        entry.update(details)
        self._history.append(entry)
        if not success and not result:
            exc = RuntimeError(f"{action} failed")
            setattr(exc, "diagnostics", result)
            raise exc
        if _should_raise(result, self._diagnostics_policy):
            _raise_with_diagnostics(result)
        return result

    def _ensure_open(self) -> None:
        if self._capsule is None:
            raise RuntimeError("session is closed")


def list_passes() -> list[str]:
    return list(_native.list_passes())


def _compile_run_pass(name: str, args: list[str], named: dict[str, Any]) -> tuple[str, list[str]]:
    canonical_name = _canonical_pass_name(name)
    compiled = list(args)
    if canonical_name == "hier-flatten":
        compiled.extend(_compile_hier_flatten_kwargs(named))
    elif canonical_name == "mem-to-reg":
        compiled.extend(_compile_mem_to_reg_kwargs(named))
    elif canonical_name == "simplify":
        compiled.extend(_compile_simplify_kwargs(named))
    elif canonical_name == "stats":
        compiled.extend(_compile_stats_kwargs(named))
    elif canonical_name == "repcut":
        compiled.extend(_compile_repcut_kwargs(named))
    elif canonical_name == "trigger-key-driven-schedule":
        compiled.extend(_compile_tkd_sched_kwargs(named))
    elif named:
        keys = ", ".join(sorted(named))
        raise TypeError(f"{name} does not accept named pass parameters: {keys}")
    return canonical_name, compiled


def _canonical_pass_name(name: str) -> str:
    value = str(name).strip()
    lowered = value.lower().replace("_", "-")
    aliases = {
        "tkd-sched": "trigger-key-driven-schedule",
    }
    return aliases.get(lowered, lowered)


def _pop_named(mapping: dict[str, Any], key: str, default: Any = None) -> Any:
    if key in mapping:
        return mapping.pop(key)
    return default


def _ensure_no_extra_named(pass_name: str, named: dict[str, Any]) -> None:
    if not named:
        return
    keys = ", ".join(sorted(named))
    raise TypeError(f"{pass_name} got unexpected named parameters: {keys}")


def _compile_hier_flatten_kwargs(named: dict[str, Any]) -> list[str]:
    local = dict(named)
    out: list[str] = []
    if _pop_named(local, "preserve_modules", False):
        out.append("-preserve-modules")
    sym_protect = _pop_named(local, "sym_protect", None)
    if sym_protect is not None:
        out.extend(["-sym-protect", str(sym_protect)])
    _ensure_no_extra_named("hier-flatten", local)
    return out


def _compile_mem_to_reg_kwargs(named: dict[str, Any]) -> list[str]:
    local = dict(named)
    out: list[str] = []
    row_limit = _pop_named(local, "row_limit", None)
    if row_limit is not None:
        out.extend(["-row-limit", str(row_limit)])
    strict_init = _pop_named(local, "strict_init", None)
    if strict_init is True:
        out.append("-strict-init")
    elif strict_init is False:
        out.append("-no-strict-init")
    _ensure_no_extra_named("mem-to-reg", local)
    return out


def _compile_simplify_kwargs(named: dict[str, Any]) -> list[str]:
    local = dict(named)
    out: list[str] = []
    max_iter = _pop_named(local, "max_iter", None)
    if max_iter is not None:
        out.extend(["-max-iter", str(max_iter)])
    x_fold = _pop_named(local, "x_fold", None)
    if x_fold is not None:
        out.extend(["-x-fold", str(x_fold)])
    semantics = _pop_named(local, "semantics", None)
    if semantics is not None:
        out.extend(["-semantics", str(semantics)])
    _ensure_no_extra_named("simplify", local)
    return out


def _compile_stats_kwargs(named: dict[str, Any]) -> list[str]:
    local = dict(named)
    out: list[str] = []
    stats_key = _pop_named(local, "statskey", None)
    if stats_key is not None:
        out.extend(["-output-key", str(stats_key)])
    _ensure_no_extra_named("stats", local)
    return out


def _compile_repcut_kwargs(named: dict[str, Any]) -> list[str]:
    local = dict(named)
    out: list[str] = []
    path = _pop_named(local, "path", None)
    if path is not None:
        out.extend(["-path", str(path)])
    partition_count = _pop_named(local, "partition_count", None)
    if partition_count is not None:
        out.extend(["-partition-count", str(partition_count)])
    imbalance_factor = _pop_named(local, "imbalance_factor", None)
    if imbalance_factor is not None:
        out.extend(["-imbalance-factor", str(imbalance_factor)])
    work_dir = _pop_named(local, "work_dir", None)
    if work_dir is not None:
        out.extend(["-work-dir", str(work_dir)])
    partitioner = _pop_named(local, "partitioner", None)
    if partitioner is not None:
        out.extend(["-partitioner", str(partitioner)])
    preset = _pop_named(local, "mtkahypar_preset", None)
    if preset is not None:
        out.extend(["-mtkahypar-preset", str(preset)])
    threads = _pop_named(local, "mtkahypar_threads", None)
    if threads is not None:
        out.extend(["-mtkahypar-threads", str(threads)])
    keep_intermediate_files = _pop_named(local, "keep_intermediate_files", None)
    if keep_intermediate_files:
        out.append("-keep-intermediate-files")
    _ensure_no_extra_named("repcut", local)
    return out


def _compile_tkd_sched_kwargs(named: dict[str, Any]) -> list[str]:
    local = dict(named)
    out: list[str] = []
    path = _pop_named(local, "path", None)
    if path is not None:
        out.extend(["-path", str(path)])
    result_key = _pop_named(local, "tkdresultkey", None)
    if result_key is not None:
        out.extend(["-result-key", str(result_key)])
    groups_key = _pop_named(local, "tkdgroupskey", None)
    if groups_key is not None:
        out.extend(["-groups-key", str(groups_key)])
    meta_key = _pop_named(local, "tkdmetakey", None)
    if meta_key is not None:
        out.extend(["-meta-key", str(meta_key)])
    _ensure_no_extra_named("tkd_sched", local)
    return out


def _normalize_diagnostics_policy(level: str) -> str:
    name = str(level).lower()
    if name == "warn":
        name = "warning"
    if name not in {"none", "warning", "error"}:
        raise ValueError("diagnostics policy must be one of: none, warning, error")
    return name


def _normalize_log_level(level: str) -> str:
    name = str(level).lower()
    if name == "warning":
        name = "warn"
    if name not in {"trace", "debug", "info", "warn", "error", "off"}:
        raise ValueError("log level must be one of: trace, debug, info, warn, error, off")
    return name


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


def _format_diagnostics(diags: list[dict]) -> str:
    if not diags:
        return "wolvrix failed"
    parts = [diag.get("text", "") for diag in diags if diag.get("text")]
    return "\n".join(parts) if parts else "wolvrix failed"


def _raise_with_diagnostics(diags: list[dict]) -> None:
    exc = RuntimeError(_format_diagnostics(diags))
    setattr(exc, "diagnostics", list(diags))
    raise exc
