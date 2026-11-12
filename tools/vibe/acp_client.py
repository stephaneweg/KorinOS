#!/usr/bin/env python3
"""Small ACP (Agent Client Protocol) client to test vibe-acp: step 0 of docs/PYTHON.md.

Standard library only, so the same script runs on Korin at step 7.

    acp_client.py [--agent CMD] [--cwd DIR] [--mode ask] [--prompt TEXT]...
                  [--mcp-test] [--stop-after initialize|new] [--sign-in] [--verbose]
    acp_client.py --app-server [--agent vibe-app-server]     (initialize only)

Runs the agent as a child with two pipes (JSON-RPC 2.0, one message per line),
then: initialize, (authenticate by browser), session/new, and one session/prompt
per --prompt. Every session/request_permission is granted once (allow_once).
The session passes when, over all prompts, the agent made at least one tool
call, asked at least one permission, answered with text, and every turn ended
with stopReason "end_turn". Exit code 0 when it passes.

--sign-in signs in with the Mistral account through the agent's
"browser-auth-delegated" method: the client prints the sign-in URL, the user
opens it in a browser, and the agent stores what it gets in $VIBE_HOME.
The client never sees nor prints a secret.

--app-server speaks to vibe-app-server instead (its own JSON-RPC protocol,
vibe/app_server/_connection_protocol.py): initialize, then the initialized
notification, and stops (step 6: the agent answers on Korin).

--step7 runs the session of step 7 (docs/PYTHON-PORT.md) in --cwd, which it
prepares (a git repository with notes.txt): read the file, modify it in "ask"
mode (permission asked and granted), "git status" and "rg" through the shell,
Escape (session/cancel during a long answer), then a second agent process
that reloads the session (session/load) and must remember the first turn.
Every step is timed; the summary goes to stderr, exit code 0 when all pass.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import shlex
import sys
import time

PROTOCOL_VERSION = 1
DEFAULT_PROMPTS = [
    "liste les fichiers",
    "crée le fichier bonjour.txt qui contient « bonjour Korin », puis dis-moi ce que tu as fait",
]


def log(*args: object) -> None:
    print(*args, file=sys.stderr, flush=True)


class AcpClient:
    def __init__(self, proc: asyncio.subprocess.Process, verbose: bool) -> None:
        self.proc = proc
        self.verbose = verbose
        self.next_id = 1
        self.pending: dict[int, asyncio.Future] = {}
        self.tool_calls: dict[str, dict] = {}
        self.permissions: list[dict] = []
        self.agent_text: list[str] = []
        self.turn_text: list[str] = []
        self.replayed: dict[str, int] = {}     # session/update kinds, counted (session/load)
        self.first_chunk = asyncio.Event()      # set at the first agent_message_chunk of a turn
        self.reader = asyncio.create_task(self._read_loop())

    async def notify(self, method: str, params: dict) -> None:
        await self._send({"jsonrpc": "2.0", "method": method, "params": params})

    async def _send(self, message: dict) -> None:
        data = json.dumps(message, ensure_ascii=False)
        if self.verbose:
            log(">>", data[:400])
        assert self.proc.stdin is not None
        self.proc.stdin.write(data.encode() + b"\n")
        await self.proc.stdin.drain()

    async def request(self, method: str, params: dict, timeout: float = 600) -> dict:
        rid = self.next_id
        self.next_id += 1
        fut = asyncio.get_running_loop().create_future()
        self.pending[rid] = fut
        await self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        return await asyncio.wait_for(fut, timeout)

    async def _read_loop(self) -> None:
        assert self.proc.stdout is not None
        while True:
            line = await self.proc.stdout.readline()
            if not line:
                for fut in self.pending.values():
                    if not fut.done():
                        fut.set_exception(EOFError("the agent closed its output"))
                return
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                log("!! not JSON on stdout:", line[:200])
                continue
            if self.verbose:
                log("<<", line.decode(errors="replace").rstrip()[:400])
            if "method" in message and "id" in message:
                asyncio.create_task(self._on_request(message))
            elif "method" in message:
                self._on_notification(message)
            elif "id" in message:
                fut = self.pending.pop(message["id"], None)
                if fut is None or fut.done():
                    continue
                if "error" in message:
                    fut.set_exception(RpcError(message["error"]))
                else:
                    fut.set_result(message.get("result") or {})

    async def _on_request(self, message: dict) -> None:
        method, params, rid = message["method"], message.get("params") or {}, message["id"]
        if method == "session/request_permission":
            options = params.get("options") or []
            choice = next((o for o in options if o.get("kind") == "allow_once"), None)
            tool = params.get("toolCall") or {}
            known = self.tool_calls.get(tool.get("toolCallId", ""), {})
            title = tool.get("title") or known.get("title") or tool.get("toolCallId")
            self.permissions.append({"tool": title, "options": [o.get("optionId") for o in options]})
            if choice is None:
                log(f"   ? permission for {title}: no allow_once option, cancelled")
                result = {"outcome": {"outcome": "cancelled"}}
            else:
                log(f"   ? permission for {title}: granted once ({choice['optionId']})")
                result = {"outcome": {"outcome": "selected", "optionId": choice["optionId"]}}
            await self._send({"jsonrpc": "2.0", "id": rid, "result": result})
            return
        # Not advertised in initialize (fs, terminal): the agent should not ask.
        log(f"!! unexpected request from the agent: {method}")
        await self._send({"jsonrpc": "2.0", "id": rid,
                          "error": {"code": -32601, "message": f"method not found: {method}"}})

    def _on_notification(self, message: dict) -> None:
        if message["method"] != "session/update":
            return
        update = (message.get("params") or {}).get("update") or {}
        kind = update.get("sessionUpdate")
        self.replayed[kind] = self.replayed.get(kind, 0) + 1
        if kind == "agent_message_chunk":
            content = update.get("content") or {}
            if content.get("type") == "text":
                self.turn_text.append(content.get("text", ""))
                self.first_chunk.set()
        elif kind == "tool_call":
            call_id = update.get("toolCallId", "?")
            self.tool_calls[call_id] = {"title": update.get("title"), "kind": update.get("kind"),
                                        "status": update.get("status")}
            log(f"   > tool {update.get('title')!r} ({update.get('kind')}) {update.get('status') or ''}")
        elif kind == "tool_call_update":
            call_id = update.get("toolCallId", "?")
            entry = self.tool_calls.setdefault(call_id, {"title": update.get("title")})
            if update.get("title"):
                entry["title"] = update["title"]
            if update.get("status") and update["status"] != entry.get("status"):
                entry["status"] = update["status"]
                log(f"   > tool {entry.get('title')!r}: {update['status']}")


class RpcError(Exception):
    def __init__(self, error: dict) -> None:
        super().__init__(f"{error.get('code')}: {error.get('message')}"
                         + (f" ({json.dumps(error['data'])[:300]})" if error.get("data") else ""))
        self.error = error


async def sign_in(client: AcpClient, methods: list[dict]) -> bool:
    ids = [m.get("id") for m in methods]
    if "browser-auth-delegated" not in ids:
        log(f"!! no browser-auth-delegated method (methods: {ids})")
        return False
    # Vibe reads the method's arguments from `_meta` (the ACP library passes
    # `_meta` as keyword arguments), not from the top level of the params.
    start = await client.request("authenticate", {"methodId": "browser-auth-delegated",
                                                  "_meta": {"action": "start"}})
    meta = (start.get("_meta") or {}).get("browser-auth-delegated") or {}
    url, attempt = meta.get("signInUrl"), meta.get("attemptId")
    if not url:
        log("   already signed in (nothing to do)")
        return True
    log("\n   Sign in with your Mistral account in a browser:\n\n   " + url + "\n")
    log(f"   (the link expires at {meta.get('expiresAt')}; waiting...)")
    # `complete` waits on the agent's side until the browser sign-in is done or
    # the link expires; it is retried on transient errors.
    deadline = time.monotonic() + 900
    while time.monotonic() < deadline:
        try:
            done = await client.request("authenticate", {
                "methodId": "browser-auth-delegated",
                "_meta": {"action": "complete", "attemptId": attempt}}, timeout=900)
            status = ((done.get("_meta") or {}).get("browser-auth-delegated") or {}).get("status")
            log(f"   sign-in: {status}")
            return status == "completed"
        except RpcError as exc:
            log(f"   not yet: {exc}")
            await asyncio.sleep(5)
    return False


INIT_PARAMS = {
    "protocolVersion": PROTOCOL_VERSION,
    "clientCapabilities": {"fs": {"readTextFile": False, "writeTextFile": False},
                           "terminal": False,
                           "_meta": {"browser-auth-delegated": True}},
    "clientInfo": {"name": "korin-acp-client", "title": "Korin ACP client", "version": "0.1"},
}

SECRET = "LUNE-2604"
NOTES = ("Korin est un système pour le Raspberry Pi 4.\n"
         f"Le mot secret de ce dossier est {SECRET}.\n")
ADDED = "Modifié par Vibe sous Korin."


async def start_agent(args: argparse.Namespace, cwd: str) -> tuple[AcpClient, dict, float]:
    """Start the agent and initialize it: (client, initialize result, seconds)."""
    t0 = time.monotonic()
    proc = await asyncio.create_subprocess_exec(
        *shlex.split(args.agent), stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=None if args.verbose else asyncio.subprocess.DEVNULL, cwd=cwd,
        limit=16 * 1024 * 1024)
    client = AcpClient(proc, args.verbose)
    init = await client.request("initialize", INIT_PARAMS, timeout=600)
    return client, init, time.monotonic() - t0


async def stop_agent(client: AcpClient) -> None:
    proc = client.proc
    if proc.stdin is not None:
        proc.stdin.close()
    try:
        await asyncio.wait_for(proc.wait(), 10)
    except (asyncio.TimeoutError, TimeoutError):
        proc.kill()
    client.reader.cancel()


async def turn(client: AcpClient, sid: str, text: str, cancel_after: float | None = None
               ) -> tuple[dict, str, float]:
    """One session/prompt: (result, answer text, seconds). With cancel_after, send
    session/cancel (Escape) that many seconds after the first chunk of the answer."""
    log(f"session/prompt: {text!r}")
    client.turn_text = []
    client.first_chunk.clear()
    t1 = time.monotonic()
    req = asyncio.ensure_future(client.request("session/prompt", {
        "sessionId": sid, "prompt": [{"type": "text", "text": text}]}))
    if cancel_after is not None:
        waiter = asyncio.ensure_future(client.first_chunk.wait())
        await asyncio.wait({req, waiter}, return_when=asyncio.FIRST_COMPLETED)
        waiter.cancel()
        if not req.done():
            await asyncio.sleep(cancel_after)
            log(f"   Escape: session/cancel ({time.monotonic() - t1:.1f} s after the prompt)")
            await client.notify("session/cancel", {"sessionId": sid})
    result = await req
    answer = "".join(client.turn_text).strip()
    dt = time.monotonic() - t1
    print(f"--- answer ({result.get('stopReason')}, {dt:.1f} s):\n{answer}\n", flush=True)
    return result, answer, dt


def git(cwd: str, *argv: str) -> str:
    import subprocess
    return subprocess.run(["git", *argv], cwd=cwd, capture_output=True, text=True).stdout


async def step7(args: argparse.Namespace) -> int:
    cwd = os.path.abspath(args.cwd)
    os.makedirs(cwd, exist_ok=True)
    notes = os.path.join(cwd, "notes.txt")
    with open(notes, "w", encoding="utf-8") as f:
        f.write(NOTES)
    if not os.path.isdir(os.path.join(cwd, ".git")):
        git(cwd, "init", "-q")
        git(cwd, "-c", "user.name=essai", "-c", "user.email=essai@korin", "add", "notes.txt")
        git(cwd, "-c", "user.name=essai", "-c", "user.email=essai@korin", "commit", "-q", "-m", "notes")
    else:
        git(cwd, "checkout", "-q", "--", "notes.txt")
    log(f"step 7 in {cwd}: git status {git(cwd, 'status', '--short')!r}")
    times: dict[str, float] = {}
    checks: dict[str, bool] = {}

    client, init, times["initialize"] = await start_agent(args, cwd)
    info = init.get("agentInfo") or {}
    log(f"initialize: {info.get('name')} {info.get('version')} ({times['initialize']:.1f} s)")
    try:
        t = time.monotonic()
        try:
            new = await client.request("session/new", {"cwd": cwd, "mcpServers": []}, timeout=600)
        except RpcError as exc:
            # -32000 Missing API key: sign in with the account (browser), then again
            if exc.error.get("code") != -32000 or not args.sign_in:
                raise
            log(f"   session/new: {exc}: sign-in")
            if not await sign_in(client, init.get("authMethods") or []):
                return 1
            t = time.monotonic()
            new = await client.request("session/new", {"cwd": cwd, "mcpServers": []}, timeout=600)
        times["session/new"] = time.monotonic() - t
        sid = new["sessionId"]
        log(f"session/new: {sid} ({times['session/new']:.1f} s)")
        await client.request("session/set_mode", {"sessionId": sid, "modeId": "ask"})
        log("session/set_mode: ask")

        # 1. read a file
        n = len(client.tool_calls)
        _, answer, times["turn: read"] = await turn(
            client, sid, "Lis le fichier notes.txt et dis-moi quel est le mot secret qu'il contient.")
        checks["read: tool call"] = len(client.tool_calls) > n
        checks["read: secret in the answer"] = SECRET in answer

        # 2. modify it in "ask" mode: a permission asked and granted
        p = len(client.permissions)
        _, answer, times["turn: edit"] = await turn(
            client, sid, f"Ajoute à la fin de notes.txt une ligne qui dit exactement : {ADDED}")
        checks["edit: permission asked and granted"] = len(client.permissions) > p
        checks["edit: file changed on disk"] = ADDED.rstrip(".") in open(notes, encoding="utf-8").read()

        # 3. git status and rg through the shell
        n = len(client.tool_calls)
        _, answer, times["turn: shell"] = await turn(
            client, sid, "Lance dans le shell `git status --short`, puis `rg -n Korin`, "
                         "et recopie-moi leurs sorties telles quelles.")
        titles = " ".join(str(c.get("title")) for c in list(client.tool_calls.values())[n:])
        log(f"   tools of the turn: {titles}")
        checks["shell: git status ran"] = "git status" in titles and "notes.txt" in answer
        checks["shell: rg ran"] = "rg -n" in titles and "1:Korin" in answer

        # 4. Escape: session/cancel during a long answer
        result, answer, times["turn: cancelled"] = await turn(
            client, sid, "Écris un très long poème de cent strophes sur l'hiver à Korin, "
                         "sans utiliser d'outil.", cancel_after=2.0)
        checks["Escape: stopReason cancelled"] = result.get("stopReason") == "cancelled"
    finally:
        await stop_agent(client)

    # 5. a new agent process reloads the session
    client, init, times["initialize (2)"] = await start_agent(args, cwd)
    try:
        client.replayed = {}
        t = time.monotonic()
        await client.request("session/load", {"sessionId": sid, "cwd": cwd, "mcpServers": []},
                             timeout=600)
        times["session/load"] = time.monotonic() - t
        log(f"session/load: {sid} ({times['session/load']:.1f} s), replayed {client.replayed}")
        checks["load: history replayed"] = client.replayed.get("user_message_chunk", 0) >= 4
        _, answer, times["turn: after load"] = await turn(
            client, sid, "Quel était le mot secret de notes.txt que tu as lu tout à l'heure ? "
                         "Réponds sans relire le fichier.")
        checks["load: remembers the first turn"] = SECRET in answer
    except RpcError as exc:
        log(f"!! session/load: {exc}")
        checks["load"] = False
    finally:
        await stop_agent(client)

    log("times: " + ", ".join(f"{k} {v:.1f} s" for k, v in times.items()))
    log("result: " + ", ".join(f"{k} {'yes' if v else 'NO'}" for k, v in checks.items()))
    return 0 if all(checks.values()) else 1


async def run(args: argparse.Namespace) -> int:
    if args.step7:
        try:
            return await step7(args)
        except RpcError as exc:
            log(f"!! error: {exc}")
            return 2
    agent = shlex.split(args.agent)
    cwd = os.path.abspath(args.cwd)
    t0 = time.monotonic()
    proc = await asyncio.create_subprocess_exec(
        *agent, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=None if args.verbose else asyncio.subprocess.DEVNULL, cwd=cwd,
        limit=16 * 1024 * 1024)
    client = AcpClient(proc, args.verbose)
    ok = True
    try:
        if args.app_server:
            init = await client.request("initialize", {
                "clientInfo": {"name": "korin-acp-client", "version": "0.1"},
                "capabilities": {}}, timeout=300)
            await client._send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
            info = init.get("serverInfo") or {}
            log(f"initialize (app-server): {info.get('name')} {info.get('version')}, "
                f"pid {init.get('processId')} ({time.monotonic() - t0:.1f} s)")
            return 0
        init = await client.request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "clientCapabilities": {"fs": {"readTextFile": False, "writeTextFile": False},
                                   "terminal": False,
                                   "_meta": {"browser-auth-delegated": True}},
            "clientInfo": {"name": "korin-acp-client", "title": "Korin ACP client",
                           "version": "0.1"},
        }, timeout=300)
        info = init.get("agentInfo") or {}
        methods = init.get("authMethods") or []
        log(f"initialize: {info.get('name')} {info.get('version')}, protocol "
            f"{init.get('protocolVersion')}, auth methods {[m.get('id') for m in methods]} "
            f"({time.monotonic() - t0:.1f} s)")
        if args.stop_after == "initialize":
            return 0
        if args.sign_in and not await sign_in(client, methods):
            return 1
        mcp_servers = []
        if args.mcp_test:
            server = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mcp_test_server.py")
            mcp_servers.append({"name": "korin", "command": sys.executable,
                                "args": [server], "env": []})
        new = await client.request("session/new", {"cwd": cwd, "mcpServers": mcp_servers},
                                   timeout=120)
        sid = new["sessionId"]
        modes = (new.get("modes") or {})
        log(f"session/new: {sid}, mode {modes.get('currentModeId')} of "
            f"{[m.get('id') for m in modes.get('availableModes') or []]} "
            f"({time.monotonic() - t0:.1f} s)")
        if args.mode:
            await client.request("session/set_mode", {"sessionId": sid, "modeId": args.mode})
            log(f"session/set_mode: {args.mode}")
        if args.stop_after == "new":
            return 0
        prompts = list(args.prompt or DEFAULT_PROMPTS)
        if args.mcp_test:
            prompts.append("Quel est le mot de Korin ? Utilise l'outil prévu.")
        for text in prompts:
            log(f"session/prompt: {text!r}")
            client.turn_text = []
            t1 = time.monotonic()
            result = await client.request("session/prompt", {
                "sessionId": sid, "prompt": [{"type": "text", "text": text}]})
            answer = "".join(client.turn_text).strip()
            client.agent_text.append(answer)
            print(f"--- answer ({result.get('stopReason')}, {time.monotonic() - t1:.1f} s):\n"
                  f"{answer}\n", flush=True)
            if result.get("stopReason") != "end_turn":
                ok = False
        passed = {
            "tool call": bool(client.tool_calls),
            "permission granted": bool(client.permissions),
            "answer": all(client.agent_text),
            "end_turn": ok,
        }
        if args.mcp_test:
            passed["MCP tool"] = "GIVRE-7431" in client.agent_text[-1]
        log("result: " + ", ".join(f"{k} {'yes' if v else 'NO'}" for k, v in passed.items()))
        return 0 if all(passed.values()) else 1
    except RpcError as exc:
        log(f"!! error: {exc}")
        return 2
    finally:
        if proc.stdin is not None:
            proc.stdin.close()
        try:
            await asyncio.wait_for(proc.wait(), 10)
        except (asyncio.TimeoutError, TimeoutError):
            proc.kill()
        client.reader.cancel()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--agent", help="agent command (default: vibe-acp, or vibe-app-server)")
    parser.add_argument("--cwd", default=".", help="working directory of the session")
    parser.add_argument("--prompt", action="append",
                        help="a turn; repeatable (default: list the files, then write one)")
    parser.add_argument("--mode", help="approval mode set after session/new (ask, accept-edits...)")
    parser.add_argument("--mcp-test", action="store_true",
                        help="add mcp_test_server.py and a turn that must call its tool")
    parser.add_argument("--stop-after", choices=["initialize", "new"],
                        help="stop after initialize or session/new")
    parser.add_argument("--app-server", action="store_true",
                        help="the agent is vibe-app-server: initialize only")
    parser.add_argument("--sign-in", action="store_true",
                        help="sign in with the Mistral account (browser) first")
    parser.add_argument("--step7", action="store_true",
                        help="the session of step 7 in --cwd (prepared as a git repository)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="show the JSON-RPC traffic and the agent's stderr")
    args = parser.parse_args()
    args.agent = args.agent or ("vibe-app-server" if args.app_server else "vibe-acp")
    return asyncio.run(run(args))


if __name__ == "__main__":
    sys.exit(main())
