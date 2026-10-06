#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

HOST="${CAP_DETECT_WEB_HOST:-0.0.0.0}"
PORT="${CAP_DETECT_WEB_PORT:-8080}"
MODEL="${CAP_MODEL:-/opt/yolov8n/police_yolov8n_dual_uint8.nb}"
CAMERA="${CAP_CAMERA:-/dev/video0}"
LOG_PATH="${CAP_WEB_LOG:-/tmp/cap_detect_web.log}"
LIVE_LOG_PATH="${CAP_LIVE_LOG:-/tmp/cap_detect_live.log}"

export CAP_MODEL="$MODEL"
export CAP_CAMERA="$CAMERA"
export CAP_LIVE_INTERVAL_SEC="${CAP_LIVE_INTERVAL_SEC:-0.4}"
export CAP_MJPEG_FPS="${CAP_MJPEG_FPS:-4}"
export CAP_PREVIEW_GAIN="${CAP_PREVIEW_GAIN:-1.55}"
export CAP_PREVIEW_GAMMA="${CAP_PREVIEW_GAMMA:-0.72}"
export CAP_PREVIEW_CLAHE="${CAP_PREVIEW_CLAHE:-2.0}"
export CAP_PREVIEW_AWB_STRENGTH="${CAP_PREVIEW_AWB_STRENGTH:-0.6}"
export CAP_PREVIEW_AWB_RED_BIAS="${CAP_PREVIEW_AWB_RED_BIAS:-0.78}"
export CAP_PREVIEW_AWB_BLUE_BIAS="${CAP_PREVIEW_AWB_BLUE_BIAS:-0.84}"
export CAP_PREVIEW_SATURATION="${CAP_PREVIEW_SATURATION:-1.55}"
export CAP_PREVIEW_HUE_SHIFT="${CAP_PREVIEW_HUE_SHIFT:-0}"
export CAP_INPUT_AWB_STRENGTH="${CAP_INPUT_AWB_STRENGTH:-0.8}"
export CAP_INPUT_AWB_RED_BIAS="${CAP_INPUT_AWB_RED_BIAS:-0.84}"
export CAP_INPUT_AWB_BLUE_BIAS="${CAP_INPUT_AWB_BLUE_BIAS:-0.88}"
export CAP_INPUT_SATURATION="${CAP_INPUT_SATURATION:-1.25}"
export CAP_INPUT_HUE_SHIFT="${CAP_INPUT_HUE_SHIFT:-0}"
export CAP_INPUT_GAIN="${CAP_INPUT_GAIN:-1.0}"
export CAP_INPUT_GAMMA="${CAP_INPUT_GAMMA:-0.92}"
export CAP_INPUT_CLAHE="${CAP_INPUT_CLAHE:-0}"
export CAP_INPUT_SHARPEN="${CAP_INPUT_SHARPEN:-1.0}"
export CAP_JPEG_QUALITY="${CAP_JPEG_QUALITY:-72}"
export CAP_LIVE_LOG="$LIVE_LOG_PATH"
export CAP_DETECT_WEB_HOST="$HOST"
export CAP_DETECT_WEB_PORT="$PORT"

if [[ ! -f "$MODEL" ]]; then
  echo "model not found: $MODEL" >&2
  exit 1
fi

if [[ ! -x "$PROJECT_ROOT/build/cap_detect" ]]; then
  echo "cap_detect binary not found or not executable: $PROJECT_ROOT/build/cap_detect" >&2
  echo "run: cmake --build build -j2" >&2
  exit 1
fi

health_check_camera() {
  local label="$1"
  echo "camera health check: ${label}"
  timeout 18 "$PROJECT_ROOT/build/cap_detect" \
    --model "$MODEL" \
    --camera "$CAMERA" \
    --snapshot /tmp/cap_detect_start_health.jpg \
    --result /tmp/cap_detect_start_health.json \
    --preview-gain "$CAP_PREVIEW_GAIN" \
    --preview-gamma "$CAP_PREVIEW_GAMMA" \
    --preview-clahe "$CAP_PREVIEW_CLAHE" \
    --preview-awb-strength "$CAP_PREVIEW_AWB_STRENGTH" \
    --preview-awb-red-bias "$CAP_PREVIEW_AWB_RED_BIAS" \
    --preview-awb-blue-bias "$CAP_PREVIEW_AWB_BLUE_BIAS" \
    --preview-saturation "$CAP_PREVIEW_SATURATION" \
    --preview-hue-shift "$CAP_PREVIEW_HUE_SHIFT" \
    --input-awb-strength "$CAP_INPUT_AWB_STRENGTH" \
    --input-awb-red-bias "$CAP_INPUT_AWB_RED_BIAS" \
    --input-awb-blue-bias "$CAP_INPUT_AWB_BLUE_BIAS" \
    --input-saturation "$CAP_INPUT_SATURATION" \
    --input-hue-shift "$CAP_INPUT_HUE_SHIFT" \
    --input-gain "$CAP_INPUT_GAIN" \
    --input-gamma "$CAP_INPUT_GAMMA" \
    --input-clahe "$CAP_INPUT_CLAHE" \
    --input-sharpen "$CAP_INPUT_SHARPEN" \
    --jpeg-quality "$CAP_JPEG_QUALITY" \
    --json >/tmp/cap_detect_start_health.stdout 2>/tmp/cap_detect_start_health.stderr
}

show_camera_health_error() {
  echo "camera health check failed" >&2
  echo "stdout:" >&2
  tail -40 /tmp/cap_detect_start_health.stdout >&2 2>/dev/null || true
  echo "stderr:" >&2
  tail -80 /tmp/cap_detect_start_health.stderr >&2 2>/dev/null || true
  echo "If stderr shows repeated 'select timeout', the A733 VIN/ISP pipeline is probably stuck; reboot the board and run this script again." >&2
}

echo "[1/5] stopping old web/live processes"
if command -v fuser >/dev/null 2>&1; then
  fuser -k "${PORT}/tcp" >/dev/null 2>&1 || true
fi
old_pids="$(pgrep -f "$PROJECT_ROOT/build/cap_detect" 2>/dev/null || true)"
if [[ -n "$old_pids" ]]; then
  kill $old_pids 2>/dev/null || true
  sleep 1
fi

echo "[2/5] preparing IMX219 camera"
sudo -v
sudo -E bash "$PROJECT_ROOT/scripts/board_prepare_camera.sh"

if [[ ! -e "$CAMERA" ]]; then
  echo "camera node not found after prepare: $CAMERA" >&2
  exit 1
fi

if [[ "${CAP_CAMERA_HEALTH_CHECK:-0}" == "1" ]]; then
  if ! health_check_camera "after camera prepare"; then
    show_camera_health_error
    echo "retrying camera prepare once" >&2
    sudo -E bash "$PROJECT_ROOT/scripts/board_prepare_camera.sh"
    if ! health_check_camera "after retry"; then
      show_camera_health_error
      exit 2
    fi
  fi
  echo "resetting camera after optional health check"
  sudo -E bash "$PROJECT_ROOT/scripts/board_prepare_camera.sh" >/tmp/cap_detect_post_health_prepare.log 2>&1
fi

echo "[3/5] starting web server on ${HOST}:${PORT}"
: > "$LOG_PATH"
nohup "$PROJECT_ROOT/scripts/start_web.sh" > "$LOG_PATH" 2>&1 < /dev/null &
web_pid=$!

ok=0
for _ in $(seq 1 30); do
  if python3 - "$PORT" <<'CHECK_STATUS' >/dev/null 2>&1
import sys, urllib.request
port = sys.argv[1]
urllib.request.urlopen(f"http://127.0.0.1:{port}/api/status", timeout=1).read()
CHECK_STATUS
  then
    ok=1
    break
  fi
  sleep 1
done

if [[ "$ok" != "1" ]]; then
  echo "web server failed to become ready; log: $LOG_PATH" >&2
  tail -80 "$LOG_PATH" >&2 || true
  exit 1
fi

echo "[4/5] starting live detection"
python3 - "$PORT" <<'START_LIVE'
import sys, urllib.request
port = sys.argv[1]
req = urllib.request.Request(f"http://127.0.0.1:{port}/api/start-live", method="POST")
print(urllib.request.urlopen(req, timeout=8).read().decode())
START_LIVE

echo "[5/5] waiting for first live frame"
frame_ok=0
for _ in $(seq 1 20); do
  frame_id="$(python3 - "$PORT" <<'READ_LIVE' 2>/dev/null || true
import json, sys, urllib.request
port = sys.argv[1]
data = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{port}/api/live", timeout=2).read().decode())
print(data.get("frame_id", 0))
READ_LIVE
)"
  if [[ "${frame_id:-0}" =~ ^[0-9]+$ ]] && [[ "$frame_id" -gt 0 ]]; then
    frame_ok=1
    break
  fi
  sleep 1
done

board_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [[ -z "${board_ip:-}" ]]; then
  board_ip="<board-ip>"
fi

echo
echo "cap_detect project is running"
echo "  web:        http://${board_ip}:${PORT}/"
echo "  model:      $MODEL"
echo "  camera:     $CAMERA"
echo "  web log:    $LOG_PATH"
echo "  live log:   $LIVE_LOG_PATH"
echo "  web pid:    $web_pid"
if [[ "$frame_ok" == "1" ]]; then
  echo "  live frame: $frame_id"
else
  echo "  live frame: failed to start" >&2
  echo "api live state:" >&2
  python3 - "$PORT" <<'READ_FAILED_LIVE' >&2 || true
import json, sys, urllib.request
port = sys.argv[1]
print(urllib.request.urlopen(f"http://127.0.0.1:{port}/api/live", timeout=3).read().decode())
READ_FAILED_LIVE
  echo "live log:" >&2
  tail -120 "$LIVE_LOG_PATH" >&2 2>/dev/null || true
  exit 3
fi
