"""watchfiles for Korin: the same API, by polling, in pure Python.

The real watchfiles is Rust (notify, inotify). Korin has neither Rust wheels
nor inotify, so this stands in for it in Vibe's site-packages. Vibe only uses
`awatch` (skill folders, pull request cache); `watch` and `Change` are kept
for compatibility.

Each poll lists the watched trees with os.scandir and compares
(mtime_ns, size) per path. The delay between polls is `poll_delay_ms`
(default 1000, or WATCHFILES_POLL_DELAY_MS): slower than the real one, but
cheap on the Pi's FAT32.
"""

from __future__ import annotations

import asyncio
import os
import time
from enum import IntEnum
from typing import AsyncGenerator, Callable, Generator, Iterable

__all__ = ["Change", "awatch", "watch", "DefaultFilter", "PythonFilter"]
VERSION = "1.2.0+korin.poll"
__version__ = VERSION

FileChange = tuple["Change", str]


class Change(IntEnum):
    added = 1
    modified = 2
    deleted = 3

    def raw_str(self) -> str:
        return self.name


class DefaultFilter:
    ignore_dirs = (
        "__pycache__", ".git", ".hg", ".svn", ".tox", ".venv", ".idea",
        "node_modules", ".mypy_cache", ".pytest_cache", ".hypothesis",
    )

    def __call__(self, change: Change, path: str) -> bool:
        parts = path.replace("\\", "/").split("/")
        return not any(p in self.ignore_dirs for p in parts)


class PythonFilter(DefaultFilter):
    def __call__(self, change: Change, path: str) -> bool:
        return path.endswith((".py", ".pyx", ".pyd")) and super().__call__(change, path)


def _default_delay() -> int:
    try:
        return max(50, int(os.environ.get("WATCHFILES_POLL_DELAY_MS", "1000")))
    except ValueError:
        return 1000


def _snapshot(
    paths: Iterable[str], recursive: bool, ignore_permission_denied: bool
) -> dict[str, tuple[int, int]]:
    seen: dict[str, tuple[int, int]] = {}

    def visit(directory: str) -> None:
        try:
            with os.scandir(directory) as entries:
                for entry in entries:
                    try:
                        st = entry.stat(follow_symlinks=False)
                    except FileNotFoundError:
                        continue
                    seen[entry.path] = (st.st_mtime_ns, st.st_size)
                    if recursive and entry.is_dir(follow_symlinks=False):
                        visit(entry.path)
        except PermissionError:
            if not ignore_permission_denied:
                raise
        except (FileNotFoundError, NotADirectoryError):
            pass

    for root in paths:
        if os.path.isdir(root):
            visit(root)
        else:
            try:
                st = os.stat(root)
                seen[root] = (st.st_mtime_ns, st.st_size)
            except FileNotFoundError:
                pass
    return seen


def _diff(
    old: dict[str, tuple[int, int]], new: dict[str, tuple[int, int]]
) -> set[FileChange]:
    changes: set[FileChange] = set()
    for path, sig in new.items():
        before = old.get(path)
        if before is None:
            changes.add((Change.added, path))
        elif before != sig:
            changes.add((Change.modified, path))
    for path in old.keys() - new.keys():
        changes.add((Change.deleted, path))
    return changes


def _check_paths(paths: tuple) -> list[str]:
    roots = [os.fspath(p) for p in paths]
    for root in roots:
        if not os.path.exists(root):
            # Same as the real watchfiles (notify refuses a missing path).
            raise FileNotFoundError(f"No such file or directory (os error 2) about {root!r}")
    return roots


async def awatch(
    *paths,
    watch_filter: Callable[[Change, str], bool] | None = DefaultFilter(),
    debounce: int = 1600,
    step: int = 50,
    stop_event=None,
    rust_timeout: int | None = None,
    yield_on_timeout: bool = False,
    debug: bool | None = None,
    raise_interrupt: bool | None = None,
    force_polling: bool | None = None,
    poll_delay_ms: int | None = None,
    recursive: bool = True,
    ignore_permission_denied: bool | None = None,
) -> AsyncGenerator[set[FileChange], None]:
    roots = _check_paths(paths)
    delay = (poll_delay_ms if poll_delay_ms else _default_delay()) / 1000
    ignore = bool(ignore_permission_denied)
    current = await asyncio.to_thread(_snapshot, roots, recursive, ignore)
    pending: set[FileChange] = set()
    first_change = 0.0
    while stop_event is None or not stop_event.is_set():
        if stop_event is not None:
            try:
                await asyncio.wait_for(stop_event.wait(), delay)
                return
            except (asyncio.TimeoutError, TimeoutError):
                pass
        else:
            await asyncio.sleep(delay)
        new = await asyncio.to_thread(_snapshot, roots, recursive, ignore)
        changes = _diff(current, new)
        current = new
        if watch_filter is not None:
            changes = {c for c in changes if watch_filter(c[0], c[1])}
        if changes:
            if not pending:
                first_change = time.monotonic()
            pending |= changes
            # Debounce: wait for one quiet poll, at most `debounce` ms.
            if (time.monotonic() - first_change) * 1000 < debounce:
                continue
        if pending:
            out, pending = pending, set()
            yield out
        elif yield_on_timeout:
            yield set()


def watch(
    *paths,
    watch_filter: Callable[[Change, str], bool] | None = DefaultFilter(),
    debounce: int = 1600,
    step: int = 50,
    stop_event=None,
    rust_timeout: int = 5000,
    yield_on_timeout: bool = False,
    debug: bool | None = None,
    raise_interrupt: bool = True,
    force_polling: bool | None = None,
    poll_delay_ms: int | None = None,
    recursive: bool = True,
    ignore_permission_denied: bool | None = None,
) -> Generator[set[FileChange], None, None]:
    roots = _check_paths(paths)
    delay = (poll_delay_ms if poll_delay_ms else _default_delay()) / 1000
    ignore = bool(ignore_permission_denied)
    current = _snapshot(roots, recursive, ignore)
    pending: set[FileChange] = set()
    first_change = 0.0
    try:
        while stop_event is None or not stop_event.is_set():
            time.sleep(delay)
            new = _snapshot(roots, recursive, ignore)
            changes = _diff(current, new)
            current = new
            if watch_filter is not None:
                changes = {c for c in changes if watch_filter(c[0], c[1])}
            if changes:
                if not pending:
                    first_change = time.monotonic()
                pending |= changes
                if (time.monotonic() - first_change) * 1000 < debounce:
                    continue
            if pending:
                out, pending = pending, set()
                yield out
            elif yield_on_timeout:
                yield set()
    except KeyboardInterrupt:
        if raise_interrupt:
            raise
