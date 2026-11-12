#!/usr/bin/env python3
"""Small ACP (Agent Client Protocol) client to test vibe-acp: step 0 of docs/PYTHON.md.

Standard library only, so the same script runs on Korin at step 7.

    acp_client.py [--agent CMD] [--cwd DIR] [--mode ask] [--prompt TEXT]...
                  [--mcp-test] [--stop-after new] [--sign-in] [--verbose]

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
        self.reader = asyncio.create_task(self._read_loop())

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
        if kind == "agent_message_chunk":
            content = update.get("content") or {}
            if content.get("type") == "text":
                self.turn_text.append(content.get("text", ""))
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
    start = await client.request("authenticate", {"methodId": "browser-auth-delegated",
                                                  "action": "start"})
    meta = (start.get("_meta") or {}).get("browser-auth-delegated") or {}
    url, attempt = meta.get("signInUrl"), meta.get("attemptId")
    if not url:
        log("   already signed in (nothing to do)")
        return True
    log("\n   Sign in with your Mistral account in a browser:\n\n   " + url + "\n")
    log(f"   (the link expires at {meta.get('expiresAt')}; waiting...)")
    deadline = time.monotonic() + 600
    while time.monotonic() < deadline:
        try:
            done = await client.request("authenticate", {"methodId": "browser-auth-delegated",
                                                         "action": "complete",
                                                         "attemptId": attempt}, timeout=120)
            status = ((done.get("_meta") or {}).get("browser-auth-delegated") or {}).get("status")
            log(f"   sign-in: {status}")
            return status == "completed"
        except RpcError as exc:
            log(f"   not yet: {exc}")
            await asyncio.sleep(5)
    return False


async def run(args: argparse.Namespace) -> int:
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
        init = await client.request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "clientCapabilities": {"fs": {"readTextFile": False, "writeTextFile": False},
                                   "terminal": False,
                                   "_meta": {"browser-auth-delegated": True}},
            "clientInfo": {"name": "korin-acp-client", "title": "Korin ACP client",
                           "version": "0.1"},
        }, timeout=120)
        info = init.get("agentInfo") or {}
        methods = init.get("authMethods") or []
        log(f"initialize: {info.get('name')} {info.get('version')}, protocol "
            f"{init.get('protocolVersion')}, auth methods {[m.get('id') for m in methods]} "
            f"({time.monotonic() - t0:.1f} s)")
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
    parser.add_argument("--agent", default="vibe-acp", help="agent command (default: vibe-acp)")
    parser.add_argument("--cwd", default=".", help="working directory of the session")
    parser.add_argument("--prompt", action="append",
                        help="a turn; repeatable (default: list the files, then write one)")
    parser.add_argument("--mode", help="approval mode set after session/new (ask, accept-edits...)")
    parser.add_argument("--mcp-test", action="store_true",
                        help="add mcp_test_server.py and a turn that must call its tool")
    parser.add_argument("--stop-after", choices=["new"], help="stop after session/new")
    parser.add_argument("--sign-in", action="store_true",
                        help="sign in with the Mistral account (browser) first")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="show the JSON-RPC traffic and the agent's stderr")
    return asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    sys.exit(main())
