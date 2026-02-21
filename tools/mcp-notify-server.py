#!/usr/bin/env python3
"""
MCP server (stdio transport) for sending WeChat Work webhook notifications.

This server exposes a single tool:
  - wechat_work_notify
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional, Tuple, Union

JSONValue = Union[Dict[str, Any], List[Any], str, int, float, bool, None]

SUPPORTED_PROTOCOL_VERSIONS = {
    "2024-11-05",
    "2025-03-26",
    "2025-06-18",
    "2025-11-25",
}

TOOL_NAME = "wechat_work_notify"


def send_wechat_message(webhook_url: str, message: str) -> Tuple[bool, str]:
    payload = {
        "msgtype": "text",
        "text": {
            "content": message
        }
    }

    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    headers = {
        "Content-Type": "application/json; charset=utf-8"
    }

    try:
        req = urllib.request.Request(
            webhook_url,
            data=data,
            headers=headers,
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=30) as response:
            response_data = json.loads(response.read().decode("utf-8"))
            if response_data.get("errcode") == 0:
                return True, ""
            errcode = response_data.get("errcode")
            errmsg = response_data.get("errmsg", "unknown error")
            return False, f"Webhook error: errcode={errcode}, errmsg={errmsg}"
    except urllib.error.HTTPError as exc:
        body = ""
        try:
            body = exc.read().decode("utf-8", errors="replace")
        except Exception:
            body = ""
        suffix = f" response={body}" if body else ""
        return False, f"HTTPError: {exc.code} {exc.reason}{suffix}"
    except urllib.error.URLError as exc:
        return False, f"URLError: {exc.reason}"
    except json.JSONDecodeError as exc:
        return False, f"JSONDecodeError: {exc}"
    except Exception as exc:
        return False, f"Unexpected error: {exc}"


def make_error(code: int, message: str, request_id: JSONValue) -> Dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "error": {
            "code": code,
            "message": message,
        },
    }


class MCPServer:
    def __init__(self, name: str, version: str, default_webhook: Optional[str], log_stderr: bool):
        self.name = name
        self.version = version
        self.default_webhook = default_webhook
        self.log_stderr = log_stderr
        self.initialized = False

    def log(self, message: str) -> None:
        if self.log_stderr:
            print(message, file=sys.stderr)

    def send_response(self, payload: JSONValue) -> None:
        sys.stdout.write(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n")
        sys.stdout.flush()

    def handle_initialize(self, request_id: JSONValue, params: JSONValue) -> Dict[str, Any]:
        if not isinstance(params, dict):
            return make_error(-32602, "Invalid params: expected object.", request_id)
        protocol_version = params.get("protocolVersion")
        if not isinstance(protocol_version, str):
            return make_error(-32602, "Missing protocolVersion.", request_id)
        if protocol_version not in SUPPORTED_PROTOCOL_VERSIONS:
            return make_error(
                -32602,
                f"Unsupported protocolVersion: {protocol_version}.",
                request_id,
            )
        self.initialized = True
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "protocolVersion": protocol_version,
                "capabilities": {
                    "tools": {
                        "listChanged": False
                    }
                },
                "serverInfo": {
                    "name": self.name,
                    "version": self.version,
                },
            },
        }

    def handle_tools_list(self, request_id: JSONValue) -> Dict[str, Any]:
        tool = {
            "name": TOOL_NAME,
            "title": "WeChat Work Notifier",
            "description": "Send a text message to a WeChat Work webhook.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "message": {
                        "type": "string",
                        "description": "Message content to send.",
                    },
                    "webhook_url": {
                        "type": "string",
                        "description": "Optional webhook URL override (defaults to WX_WEBHOOK_URL).",
                    },
                },
                "required": ["message"],
            },
            "outputSchema": {
                "type": "object",
                "properties": {
                    "ok": {"type": "boolean"},
                    "error": {"type": "string"},
                },
                "required": ["ok"],
            },
        }
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "tools": [tool],
            },
        }

    def handle_tools_call(self, request_id: JSONValue, params: JSONValue) -> Dict[str, Any]:
        if not isinstance(params, dict):
            return make_error(-32602, "Invalid params: expected object.", request_id)
        name = params.get("name")
        if name != TOOL_NAME:
            return make_error(-32602, f"Unknown tool: {name}", request_id)
        arguments = params.get("arguments", {})
        if arguments is None:
            arguments = {}
        if not isinstance(arguments, dict):
            return make_error(-32602, "Invalid params: arguments must be object.", request_id)

        message = arguments.get("message")
        if not isinstance(message, str) or not message.strip():
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "result": {
                    "content": [
                        {"type": "text", "text": "Invalid message: must be a non-empty string."}
                    ],
                    "structuredContent": {
                        "ok": False,
                        "error": "Invalid message: must be a non-empty string.",
                    },
                    "isError": True,
                },
            }

        webhook_url = arguments.get("webhook_url")
        if webhook_url is None:
            webhook_url = self.default_webhook or os.environ.get("WX_WEBHOOK_URL")
        if not isinstance(webhook_url, str) or not webhook_url.strip():
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "result": {
                    "content": [
                        {"type": "text", "text": "Missing webhook URL (WX_WEBHOOK_URL or webhook_url)."}
                    ],
                    "structuredContent": {
                        "ok": False,
                        "error": "Missing webhook URL (WX_WEBHOOK_URL or webhook_url).",
                    },
                    "isError": True,
                },
            }

        success, error_message = send_wechat_message(webhook_url.strip(), message)
        if success:
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "result": {
                    "content": [{"type": "text", "text": "Sent WeChat Work message."}],
                    "structuredContent": {"ok": True},
                },
            }

        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "content": [
                    {"type": "text", "text": f"Failed to send message: {error_message}"}
                ],
                "structuredContent": {"ok": False, "error": error_message},
                "isError": True,
            },
        }

    def handle_message(self, message: JSONValue) -> Optional[Dict[str, Any]]:
        if not isinstance(message, dict):
            return make_error(-32600, "Invalid Request.", None)
        if message.get("jsonrpc") != "2.0":
            return make_error(-32600, "Invalid Request.", message.get("id"))

        method = message.get("method")
        has_id = "id" in message
        request_id = message.get("id") if has_id else None
        params = message.get("params", {})

        if not isinstance(method, str):
            return make_error(-32600, "Invalid Request.", request_id)

        if not has_id:
            if method == "notifications/initialized":
                self.initialized = True
                return None
            return None
        if request_id is None:
            return make_error(-32600, "Invalid Request: id must not be null.", None)

        if method == "initialize":
            return self.handle_initialize(request_id, params)
        if method == "tools/list":
            return self.handle_tools_list(request_id)
        if method == "tools/call":
            return self.handle_tools_call(request_id, params)

        return make_error(-32601, f"Method not found: {method}", request_id)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="MCP stdio server for WeChat Work notifications",
    )
    parser.add_argument(
        "--name",
        default="wolf-sv-parser-notify",
        help="Server name for MCP initialize response.",
    )
    parser.add_argument(
        "--version",
        default="0.1.0",
        help="Server version for MCP initialize response.",
    )
    parser.add_argument(
        "--webhook-url",
        default=None,
        help="Default webhook URL (overrides WX_WEBHOOK_URL).",
    )
    parser.add_argument(
        "--log-stderr",
        action="store_true",
        help="Enable stderr logging.",
    )
    args = parser.parse_args()

    server = MCPServer(
        name=args.name,
        version=args.version,
        default_webhook=args.webhook_url,
        log_stderr=args.log_stderr,
    )

    stream = sys.stdin.buffer
    while True:
        line = stream.readline()
        if not line:
            break
        stripped = line.strip()
        if not stripped:
            continue

        if stripped.lower().startswith(b"content-length:"):
            try:
                length = int(stripped.split(b":", 1)[1].strip())
            except ValueError:
                server.send_response(make_error(-32600, "Invalid Content-Length header.", None))
                continue

            while True:
                header_line = stream.readline()
                if not header_line:
                    return 0
                if header_line in (b"\n", b"\r\n"):
                    break

            body = stream.read(length)
            if not body:
                break
            raw_message = body.decode("utf-8", errors="replace")
        else:
            raw_message = stripped.decode("utf-8", errors="replace")

        try:
            message = json.loads(raw_message)
        except json.JSONDecodeError:
            server.send_response(make_error(-32700, "Parse error.", None))
            continue

        responses: List[Dict[str, Any]] = []
        if isinstance(message, list):
            for item in message:
                response = server.handle_message(item)
                if response is not None:
                    responses.append(response)
        else:
            response = server.handle_message(message)
            if response is not None:
                responses.append(response)

        if not responses:
            continue
        if len(responses) == 1:
            server.send_response(responses[0])
        else:
            server.send_response(responses)

    return 0


if __name__ == "__main__":
    sys.exit(main())
