#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
host="${CAP_DETECT_WEB_HOST:-0.0.0.0}"
port="${CAP_DETECT_WEB_PORT:-8080}"
python3 web/server.py --host "$host" --port "$port"
