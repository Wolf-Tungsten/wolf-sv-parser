from __future__ import annotations

from . import _wolvrix as _native

__all__ = [
    "Design",
    "from_json_string",
    "list_passes",
    "read_json",
    "read_sv",
]


class Design:
    def __init__(self, capsule):
        self._capsule = capsule

    def run_pass(self, name: str, args: list[str] | None = None, dryrun: bool = False) -> bool:
        return bool(_native.run_pass(self._capsule, name, args or [], dryrun))

    def write_sv(self, output: str, top: list[str] | None = None) -> None:
        _native.write_sv(self._capsule, output, top or [])

    def to_json(self, mode: str = "pretty-compact", top: list[str] | None = None) -> str:
        return _native.store_json_string(self._capsule, mode, top or [])

    def write_json(self, output: str, mode: str = "pretty-compact", top: list[str] | None = None) -> None:
        text = self.to_json(mode=mode, top=top)
        with open(output, "w", encoding="utf-8") as handle:
            handle.write(text)


def read_sv(path: str | None, slang_args: list[str] | None = None, log_level: str = "warn") -> Design:
    return Design(_native.read_sv(path, slang_args or [], log_level))


def read_json(path: str) -> Design:
    return Design(_native.read_json(path))


def from_json_string(text: str) -> Design:
    return Design(_native.load_json_string(text))


def list_passes() -> list[str]:
    return list(_native.list_passes())
