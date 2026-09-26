#!/usr/bin/env python3
"""Bridge an stdio-only MCP client to the HTTP server built into Remere's Map Editor.

Most clients (Claude Code, Claude Desktop, Codex) can talk to the editor's
endpoint directly with an `http` transport, and you should prefer that. Use this
script only for a client that speaks nothing but stdio:

    python tools/mcp_stdio_bridge.py http://127.0.0.1:7331/mcp

It reads newline-delimited JSON-RPC from stdin, forwards each message to the
editor over HTTP, and writes the replies back to stdout. Standard library only,
no dependencies.
"""

import json
import sys
import urllib.error
import urllib.request

DEFAULT_URL = "http://127.0.0.1:7331/mcp"
TIMEOUT_SECONDS = 120


def forward(url: str, payload: bytes) -> bytes:
    request = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
        return response.read()


def error_response(message_id, message: str) -> dict:
    return {
        "jsonrpc": "2.0",
        "id": message_id,
        "error": {"code": -32603, "message": message},
    }


def main() -> int:
    url = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        # Notifications carry no id and must not get a reply, so the id is
        # needed both to answer and to stay quiet at the right times.
        try:
            message_id = json.loads(line).get("id")
        except json.JSONDecodeError:
            message_id = None

        try:
            body = forward(url, line.encode("utf-8"))
        except urllib.error.URLError as exc:
            if message_id is None:
                continue
            body = json.dumps(
                error_response(
                    message_id,
                    f"cannot reach Remere's Map Editor at {url} ({exc.reason}). "
                    "Is the editor running with the MCP server enabled?",
                )
            ).encode("utf-8")
        except Exception as exc:  # noqa: BLE001 - never kill the bridge on one message
            if message_id is None:
                continue
            body = json.dumps(error_response(message_id, str(exc))).encode("utf-8")

        text = body.decode("utf-8").strip()
        if not text:
            # 202 Accepted: the editor took a notification and has nothing to say.
            continue

        sys.stdout.write(text + "\n")
        sys.stdout.flush()

    return 0


if __name__ == "__main__":
    sys.exit(main())
