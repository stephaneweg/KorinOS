"""SQLite sans WAL pour Korin OS.

Le mode WAL de SQLite partage son index par un fichier « -shm » projeté en mémoire (mmap partagé) : sous Korin,
l'ouverture échoue (« disk I/O error »). Les bases d'OpenJarvis demandent toutes `PRAGMA journal_mode=WAL` ; ce
module, chargé au démarrage de Python par `korin_sqlite_nowal.pth` (dans le site-packages du projet seulement),
transforme cette demande en `journal_mode=DELETE`, le journal classique, qui marche.

On n'utilise pas le mode WAL exclusif (`locking_mode=EXCLUSIVE`, qui se passe de -shm) : sous Korin les verrous de
fichiers sont sans effet, et deux process (jarvis serve et jarvis ask) écriraient chacun dans la base sans se voir.
"""

from __future__ import annotations

import re
import sqlite3

_WAL = re.compile(r"(journal_mode\s*=\s*)['\"]?wal['\"]?", re.IGNORECASE)


def _rewrite(sql):
    if isinstance(sql, str) and "journal_mode" in sql.lower():
        return _WAL.sub(r"\1DELETE", sql)
    return sql


class Connection(sqlite3.Connection):
    """Connexion qui remplace journal_mode=WAL par DELETE."""

    def execute(self, sql, *args, **kwargs):
        return super().execute(_rewrite(sql), *args, **kwargs)

    def executescript(self, sql):
        return super().executescript(_rewrite(sql))

    def cursor(self, factory=None):
        return super().cursor(factory or Cursor)


class Cursor(sqlite3.Cursor):
    def execute(self, sql, *args, **kwargs):
        return super().execute(_rewrite(sql), *args, **kwargs)

    def executescript(self, sql):
        return super().executescript(_rewrite(sql))


_connect = sqlite3.connect


def connect(*args, **kwargs):
    factory = kwargs.get("factory")
    if factory is None or factory is sqlite3.Connection:
        kwargs["factory"] = Connection
    elif not issubclass(factory, Connection):
        kwargs["factory"] = type("Korin" + factory.__name__, (Connection, factory), {})
    return _connect(*args, **kwargs)


if getattr(sqlite3.connect, "__module__", "") != __name__:
    sqlite3.connect = connect
