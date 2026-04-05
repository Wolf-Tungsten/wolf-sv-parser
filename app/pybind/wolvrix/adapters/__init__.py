from __future__ import annotations

import importlib
import pkgutil

from ._registry import adapt_session_value

__all__ = [
    "adapt_session_value",
]


def _load_builtin_adapters() -> None:
    for module_info in pkgutil.iter_modules(__path__):
        name = module_info.name
        if name.startswith("_"):
            continue
        importlib.import_module(f"{__name__}.{name}")


_load_builtin_adapters()
