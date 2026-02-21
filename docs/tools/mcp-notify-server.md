# MCP WeChat Work Notify Server

This repository includes a small MCP (stdio) server that exposes a single tool
for sending WeChat Work webhook notifications.

## File
- `tools/mcp-notify-server.py`

## Environment
- `WX_WEBHOOK_URL` (default webhook URL)

## Run (stdio)
```bash
python3 tools/mcp-notify-server.py
```

Optional flags:
```bash
python3 tools/mcp-notify-server.py --log-stderr --name wolf-sv-parser-notify --version 0.1.0
```

## Tool
- `wechat_work_notify`
  - `message` (string, required)
  - `webhook_url` (string, optional, overrides `WX_WEBHOOK_URL`)

## Notes
- Supported protocol versions: 2024-11-05, 2025-03-26, 2025-06-18, 2025-11-25.
- Accepts either line-delimited JSON-RPC messages or `Content-Length` framed input.
- On failure, the tool returns `isError: true` with an error message.
