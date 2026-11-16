"""jiter de Korin : le remplaçant en Python pur de jiter (un module Rust), pour le SDK anthropic (docs/CLAUDE-ACP.md).

Le SDK ne s'en sert que pour lire l'entrée d'un appel d'outil pendant le flux : from_json (octets, partial_mode=...).
Le JSON complet passe par json.loads ; un JSON coupé à la fin (le flux n'est pas fini) est lu jusqu'où il va :
objets et tableaux fermés, la chaîne coupée gardée en partial_mode="trailing-strings", jetée sinon. claude-acp
n'envoie pas ses outils en eager_input_streaming quand ce module est le remplaçant (__korin_shim__) : l'entrée
arrive d'un bloc et n'est relue qu'une fois.
"""
import json

__version__ = "0.12.0+korin"
__korin_shim__ = True

_WS = " \t\r\n"
_END = object()


class _Eof(Exception):
    pass


def from_json(json_data, *, allow_inf_nan=True, cache_mode=True, partial_mode=False, catch_duplicate_keys=False,
              float_mode="float", lossless_floats=False):
    s = json_data.decode("utf-8") if isinstance(json_data, (bytes, bytearray, memoryview)) else json_data
    try:
        return json.loads(s)
    except ValueError:
        if partial_mode in (False, "off", None):
            raise
    trailing = partial_mode == "trailing-strings"
    p = _Parser(s, trailing)
    p.skip()
    if p.i >= len(s):
        raise ValueError("EOF while parsing a value at line 1 column 0")
    v = p.value()
    if v is _END:
        raise ValueError("EOF while parsing a value")
    p.skip()
    if p.i < len(s):
        raise ValueError("trailing characters at column %d" % (p.i + 1))
    return v


def cache_clear():
    pass


def cache_usage():
    return 0


class _Parser:
    def __init__(self, s, trailing):
        self.s = s
        self.i = 0
        self.trailing = trailing

    def skip(self):
        s, n = self.s, len(self.s)
        while self.i < n and s[self.i] in _WS:
            self.i += 1

    def value(self):
        """Une valeur ; _END si le texte s'arrête avant qu'on puisse en garder quelque chose."""
        self.skip()
        s = self.s
        if self.i >= len(s):
            return _END
        c = s[self.i]
        if c == "{":
            return self.obj()
        if c == "[":
            return self.arr()
        if c == '"':
            v, done = self.string()
            return v if done or self.trailing else _END
        for word, val in (("true", True), ("false", False), ("null", None)):
            if s.startswith(word, self.i):
                self.i += len(word)
                return val
            if word.startswith(s[self.i:]):
                self.i = len(s)
                return _END
        return self.number()

    def obj(self):
        s = self.s
        self.i += 1
        out = {}
        while True:
            self.skip()
            if self.i >= len(s):
                return out
            if s[self.i] == "}":
                self.i += 1
                return out
            if s[self.i] != '"':
                raise ValueError("key must be a string at column %d" % (self.i + 1))
            k, done = self.string()
            if not done:
                return out
            self.skip()
            if self.i >= len(s):
                return out
            if s[self.i] != ":":
                raise ValueError("expected ':' at column %d" % (self.i + 1))
            self.i += 1
            v = self.value()
            if v is _END:
                return out
            out[k] = v
            self.skip()
            if self.i >= len(s):
                return out
            if s[self.i] == ",":
                self.i += 1
            elif s[self.i] == "}":
                self.i += 1
                return out
            else:
                raise ValueError("expected ',' or '}' at column %d" % (self.i + 1))

    def arr(self):
        s = self.s
        self.i += 1
        out = []
        while True:
            self.skip()
            if self.i >= len(s):
                return out
            if s[self.i] == "]":
                self.i += 1
                return out
            v = self.value()
            if v is _END:
                return out
            out.append(v)
            self.skip()
            if self.i >= len(s):
                return out
            if s[self.i] == ",":
                self.i += 1
            elif s[self.i] == "]":
                self.i += 1
                return out
            else:
                raise ValueError("expected ',' or ']' at column %d" % (self.i + 1))

    def string(self):
        """(texte, fini) ; une chaîne coupée rend ce qu'elle a (sans un échappement coupé)."""
        s = self.s
        start = self.i
        j = start + 1
        n = len(s)
        while j < n:
            c = s[j]
            if c == "\\":
                j += 2
                continue
            if c == '"':
                self.i = j + 1
                return json.loads(s[start:j + 1]), True
            j += 1
        self.i = n
        body = s[start + 1:]
        # un échappement coupé (« \ », « \u12 ») : retiré ; le dernier échappement est cherché depuis le début
        # (« \\ » est complet)
        k, last = 0, -1
        while k < len(body):
            if body[k] == "\\":
                last = k
                k += 2
            else:
                k += 1
        if last >= 0:
            tail = body[last:]
            if len(tail) < 2 or (tail[1] == "u" and len(tail) < 6):
                body = body[:last]
        try:
            return json.loads('"' + body + '"'), False
        except ValueError:
            return body, False

    def number(self):
        s = self.s
        j = self.i
        n = len(s)
        while j < n and s[j] in "+-0123456789.eE":
            j += 1
        if j == self.i:
            raise ValueError("expected value at column %d" % (self.i + 1))
        text = s[self.i:j]
        self.i = j
        try:
            return json.loads(text)
        except ValueError:
            if j < n:
                raise
            return _END     # un nombre coupé (« 12. », « 1e ») : jeté, comme jiter
