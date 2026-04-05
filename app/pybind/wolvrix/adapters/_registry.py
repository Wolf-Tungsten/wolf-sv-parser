from __future__ import annotations

from collections.abc import Callable
from typing import Any, TYPE_CHECKING

if TYPE_CHECKING:
    from .. import Session


SessionAdapter = Callable[["Session", str, str, str, Any], Any]

_ADAPTERS: dict[tuple[str, str], SessionAdapter] = {}


def register_session_adapter(*, storage: str, kind: str, factory: SessionAdapter) -> None:
    _ADAPTERS[(str(storage), str(kind))] = factory


def adapt_session_value(session: "Session", *, key: str, storage: str, kind: str, payload: Any) -> Any:
    factory = _ADAPTERS.get((str(storage), str(kind)))
    if factory is None:
        return None
    return factory(session, str(key), str(storage), str(kind), payload)
