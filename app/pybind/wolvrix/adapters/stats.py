from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Any

from .. import _wolvrix as _native
from ._registry import register_session_adapter


@dataclass(frozen=True)
class StatsValue:
    key: str
    text: str

    def to_dict(self) -> dict[str, Any]:
        return dict(json.loads(self.text))

    def to_json(self, *, pretty: bool = False) -> str:
        if not pretty:
            return self.text
        return json.dumps(self.to_dict(), ensure_ascii=True, indent=2, sort_keys=True)

    def write_json(self, path: str) -> None:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(self.to_json(pretty=True))

    def __repr__(self) -> str:
        return f"StatsValue(key={self.key!r})"


def _adapt_stats(session, key: str, _storage: str, _kind: str, _payload):
    text = _native.session_export(session._capsule, key=key, view="text")
    return StatsValue(key=key, text=text)


register_session_adapter(storage="native", kind="stats", factory=_adapt_stats)
