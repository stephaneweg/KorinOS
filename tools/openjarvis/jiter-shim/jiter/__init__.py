"""jiter pour Korin : remplaçant en pur Python du parseur JSON en Rust de pydantic (jiter).

Le vrai jiter se construit avec maturin, qui ne se construit pas sous Korin. Le paquet openai n'en prend que
`from_json` (pour lire du JSON incomplet pendant un flux) : ce module fournit cette fonction, avec le mode partiel
(`partial_mode=True` ou "trailing-strings"), sur le module json de Python.
"""

from __future__ import annotations

import json
from typing import Any, Literal, Union

__version__ = "0.0.0+korin"

PartialMode = Union[bool, Literal["off", "on", "trailing-strings"]]


def _complete(text: str, keep_trailing_strings: bool) -> str:
    """Ferme un JSON tronqué : chaîne, objet ou tableau ouverts, clé sans valeur, virgule pendante."""
    stack: list[str] = []
    in_string = False
    escape = False
    string_start = -1
    for i, ch in enumerate(text):
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            string_start = i
        elif ch in "{[":
            stack.append(ch)
        elif ch in "}]":
            if stack:
                stack.pop()

    out = text
    if in_string:
        if keep_trailing_strings:
            if escape:
                out = out[:-1]
            out += '"'
        else:
            out = out[:string_start]
    out = out.rstrip()

    # Retire ce qui ne peut pas finir une valeur : virgule, deux-points (et la clé qui le précède), littéral tronqué.
    while True:
        stripped = out.rstrip()
        if stripped.endswith(","):
            out = stripped[:-1]
            continue
        if stripped.endswith(":"):
            body = stripped[:-1].rstrip()
            if body.endswith('"'):
                j = body.rfind('"', 0, len(body) - 1)
                while j > 0 and body[j - 1] == "\\":
                    j = body.rfind('"', 0, j - 1)
                body = body[:j] if j >= 0 else body
            out = body
            continue
        if stack and stack[-1] == "{" and stripped.endswith('"'):
            # Une clé seule, sans deux-points : on l'enlève.
            j = stripped.rfind('"', 0, len(stripped) - 1)
            before = stripped[:j].rstrip() if j >= 0 else stripped
            if before.endswith("{") or before.endswith(","):
                out = before
                continue
        # Littéral ou nombre tronqué (tru, nul, 1.e) : on enlève le mot en cours s'il ne se lit pas.
        k = len(stripped)
        while k > 0 and stripped[k - 1] not in ",:[{}]\" \t\r\n":
            k -= 1
        word = stripped[k:]
        if word:
            try:
                json.loads(word)
            except ValueError:
                out = stripped[:k]
                continue
        break

    for opener in reversed(stack):
        out += "}" if opener == "{" else "]"
    return out


def from_json(
    json_data: bytes | bytearray | str,
    /,
    *,
    allow_inf_nan: bool = True,
    cache_mode: Any = "all",
    partial_mode: PartialMode = False,
    catch_duplicate_keys: bool = False,
    float_mode: Any = "float",
) -> Any:
    """Lit un document JSON ; en mode partiel, accepte un document tronqué (comme jiter.from_json)."""
    if isinstance(json_data, (bytes, bytearray)):
        text = bytes(json_data).decode("utf-8")
    else:
        text = json_data
    mode = {"off": False, "on": True}.get(partial_mode, partial_mode) if isinstance(partial_mode, str) else partial_mode
    try:
        return json.loads(text)
    except ValueError:
        if not mode:
            raise
    completed = _complete(text, keep_trailing_strings=(mode == "trailing-strings"))
    if not completed.strip():
        return None
    return json.loads(completed)


def cache_clear() -> None:
    """Sans effet : ce remplaçant n'a pas de cache de chaînes."""


def cache_usage() -> int:
    return 0


class LosslessFloat(float):
    """Flottant gardé tel quel (simplifié : un float ordinaire)."""
