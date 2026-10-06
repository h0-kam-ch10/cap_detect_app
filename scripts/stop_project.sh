#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${CAP_DETECT_WEB_PORT:-8080}"
STOP_TIMEOUT="${CAP_STOP_TIMEOUT:-5}"
WEB_URL="http://127.0.0.1:${PORT}"

log() {
  printf '[shutdown] %s\n' "$*"
}

post_stop_live() {
  if ! command -v python3 >/dev/null 2>&1; then
    return 0
  fi
  python3 - "$WEB_URL" <<'PY_STOP_LIVE' >/dev/null 2>&1 || true
import sys
import urllib.request
url = sys.argv[1] + "/api/stop-live"
req = urllib.request.Request(url, method="POST")
urllib.request.urlopen(req, timeout=3).read()
PY_STOP_LIVE
}

collect_pids() {
  local pattern="$1"
  pgrep -f "$pattern" 2>/dev/null | awk -v self="$$" '$1 != self {print $1}' || true
}

wait_pids_gone() {
  local pids="$1"
  local waited=0
  if [[ -z "$pids" ]]; then
    return 0
  fi
  while [[ "$waited" -lt "$STOP_TIMEOUT" ]]; do
    local alive=""
    for pid in $pids; do
      if kill -0 "$pid" 2>/dev/null; then
        alive=1
        break
      fi
    done
    if [[ -z "$alive" ]]; then
      return 0
    fi
    sleep 1
    waited=$((waited + 1))
  done
  return 1
}

terminate_pids() {
  local label="$1"
  local pids="$2"
  if [[ -z "$pids" ]]; then
    log "${label}: no process found"
    return 0
  fi

  log "${label}: SIGTERM ${pids}"
  kill $pids 2>/dev/null || true
  if wait_pids_gone "$pids"; then
    log "${label}: stopped"
    return 0
  fi

  log "${label}: still alive after ${STOP_TIMEOUT}s, SIGKILL ${pids}"
  kill -KILL $pids 2>/dev/null || true
  wait_pids_gone "$pids" || true
}

stop_port_users() {
  if command -v fuser >/dev/null 2>&1; then
    local users
    users="$(fuser "${PORT}/tcp" 2>/dev/null || true)"
    if [[ -n "$users" ]]; then
      log "port ${PORT}: SIGTERM users ${users}"
      fuser -TERM -k "${PORT}/tcp" >/dev/null 2>&1 || true
      sleep 1
      users="$(fuser "${PORT}/tcp" 2>/dev/null || true)"
      if [[ -n "$users" ]]; then
        log "port ${PORT}: SIGKILL remaining users ${users}"
        fuser -KILL -k "${PORT}/tcp" >/dev/null 2>&1 || true
      fi
    else
      log "port ${PORT}: no listener"
    fi
  fi
}

cleanup_runtime_files() {
  # Keep latest.jpg/latest.json for post-run inspection. Remove only transient temp files.
  rm -f \
    "${PROJECT_ROOT}/web/static/latest.jpg.tmp.jpg" \
    "${PROJECT_ROOT}/web/static/latest.json.tmp" \
    /tmp/cap_detect_start_health.jpg.tmp.jpg \
    /tmp/cap_detect_start_health.json.tmp \
    /tmp/threaded_latest.jpg.tmp.jpg \
    /tmp/threaded_latest.json.tmp 2>/dev/null || true
}

log "project root: ${PROJECT_ROOT}"
log "requesting live process shutdown through Web API"
post_stop_live

cap_pids="$(collect_pids "${PROJECT_ROOT}/build/cap_detect")"
terminate_pids "cap_detect live/native process" "$cap_pids"

web_pids="$(collect_pids "${PROJECT_ROOT}/web/server.py")"
terminate_pids "web server" "$web_pids"

start_web_pids="$(collect_pids "${PROJECT_ROOT}/scripts/start_web.sh")"
terminate_pids "start_web wrapper" "$start_web_pids"

stop_port_users
cleanup_runtime_files

remaining_cap="$(collect_pids "${PROJECT_ROOT}/build/cap_detect")"
remaining_web="$(collect_pids "${PROJECT_ROOT}/web/server.py")"
remaining_port=""
if command -v fuser >/dev/null 2>&1; then
  remaining_port="$(fuser "${PORT}/tcp" 2>/dev/null || true)"
fi

if [[ -n "$remaining_cap$remaining_web$remaining_port" ]]; then
  log "warning: shutdown finished with remaining resources"
  [[ -n "$remaining_cap" ]] && log "remaining cap_detect pids: ${remaining_cap}"
  [[ -n "$remaining_web" ]] && log "remaining web pids: ${remaining_web}"
  [[ -n "$remaining_port" ]] && log "remaining port ${PORT} users: ${remaining_port}"
  exit 1
fi

log "all project processes stopped"
log "native threads, V4L2 mmap buffers and process memory are released by graceful process exit; forced kills are reclaimed by the kernel"
