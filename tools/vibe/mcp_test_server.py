#!/usr/bin/env python3
"""Minimal MCP server on stdio (standard library only), to test that an agent
without code mode still sees and calls MCP tools.

One tool, `korin_word`: returns a fixed word the client can look for in the
agent's answer. Used by acp_client.py --mcp-test.
"""

import json
import sys

WORD = "GIVRE-7431"
TOOL = {
    "name": "korin_word",
    "description": "Returns the Korin test word. Call it when asked for the Korin word.",
    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
}


def reply(rid, result=None, error=None):
    message = {"jsonrpc": "2.0", "id": rid}
    if error is not None:
        message["error"] = error
    else:
        message["result"] = result
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


for line in sys.stdin:
    try:
        request = json.loads(line)
    except json.JSONDecodeError:
        continue
    method, rid = request.get("method"), request.get("id")
    if rid is None:  # notification (notifications/initialized...)
        continue
    if method == "initialize":
        version = (request.get("params") or {}).get("protocolVersion", "2025-06-18")
        reply(rid, {"protocolVersion": version,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "korin-test", "version": "0.1"}})
    elif method == "tools/list":
        reply(rid, {"tools": [TOOL]})
    elif method == "tools/call":
        name = (request.get("params") or {}).get("name")
        if name == TOOL["name"]:
            reply(rid, {"content": [{"type": "text", "text": WORD}], "isError": False})
        else:
            reply(rid, error={"code": -32602, "message": f"unknown tool {name}"})
    elif method == "ping":
        reply(rid, {})
    else:
        reply(rid, error={"code": -32601, "message": f"method not found: {method}"})
