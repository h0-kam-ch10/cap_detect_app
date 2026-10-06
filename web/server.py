#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

PROJECT_ROOT = Path(__file__).resolve().parents[1]
STATIC_ROOT = PROJECT_ROOT / "web" / "static"
DEFAULT_MODEL = os.environ.get("CAP_MODEL", "/opt/yolov8n/police_yolov8n_dual_uint8.nb")
DEFAULT_INPUT = os.environ.get("CAP_INPUT", "/opt/yolov8n/traffic_police_01_chw_rgb_320.dat")
DEFAULT_CAMERA = os.environ.get("CAP_CAMERA", "/dev/video0")
SNAPSHOT_PATH = os.environ.get("CAP_SNAPSHOT", str(STATIC_ROOT / "latest.jpg"))
LIVE_RESULT_PATH = Path(os.environ.get("CAP_LIVE_RESULT", str(STATIC_ROOT / "latest.json")))
LIVE_LOG_PATH = Path(os.environ.get("CAP_LIVE_LOG", "/tmp/cap_detect_live.log"))
EVENT_LOG_PATH = Path(os.environ.get("CAP_EVENT_LOG", str(PROJECT_ROOT / "data" / "events.jsonl")))
LIVE_INTERVAL_SEC = float(os.environ.get("CAP_LIVE_INTERVAL_SEC", "0.4"))
MJPEG_FPS = float(os.environ.get("CAP_MJPEG_FPS", "4"))
CLASS_SCALE = float(os.environ.get("CAP_CLASS_SCALE", "1"))
PREVIEW_GAIN = float(os.environ.get("CAP_PREVIEW_GAIN", "1.55"))
PREVIEW_GAMMA = float(os.environ.get("CAP_PREVIEW_GAMMA", "0.72"))
PREVIEW_CLAHE = float(os.environ.get("CAP_PREVIEW_CLAHE", "2.0"))
PREVIEW_AWB_STRENGTH = float(os.environ.get("CAP_PREVIEW_AWB_STRENGTH", "0.6"))
PREVIEW_AWB_RED_BIAS = float(os.environ.get("CAP_PREVIEW_AWB_RED_BIAS", "0.78"))
PREVIEW_AWB_BLUE_BIAS = float(os.environ.get("CAP_PREVIEW_AWB_BLUE_BIAS", "0.84"))
PREVIEW_SATURATION = float(os.environ.get("CAP_PREVIEW_SATURATION", "1.55"))
PREVIEW_HUE_SHIFT = float(os.environ.get("CAP_PREVIEW_HUE_SHIFT", "0"))
INPUT_AWB_STRENGTH = float(os.environ.get("CAP_INPUT_AWB_STRENGTH", "0.8"))
INPUT_AWB_RED_BIAS = float(os.environ.get("CAP_INPUT_AWB_RED_BIAS", "0.84"))
INPUT_AWB_BLUE_BIAS = float(os.environ.get("CAP_INPUT_AWB_BLUE_BIAS", "0.88"))
INPUT_SATURATION = float(os.environ.get("CAP_INPUT_SATURATION", "1.25"))
INPUT_HUE_SHIFT = float(os.environ.get("CAP_INPUT_HUE_SHIFT", "0"))
INPUT_GAIN = float(os.environ.get("CAP_INPUT_GAIN", "1.0"))
INPUT_GAMMA = float(os.environ.get("CAP_INPUT_GAMMA", "0.92"))
INPUT_CLAHE = float(os.environ.get("CAP_INPUT_CLAHE", "0"))
INPUT_SHARPEN = float(os.environ.get("CAP_INPUT_SHARPEN", "1.0"))
JPEG_QUALITY = int(os.environ.get("CAP_JPEG_QUALITY", "72"))

LATEST_RESULT = {
    "ok": None,
    "timestamp": None,
    "command": None,
    "stdout": "",
    "stderr": "",
    "returncode": None,
}

LIVE_STATE = {
    "running": False,
    "frame_id": 0,
    "last_update": None,
    "result": None,
    "error": None,
}
LIVE_LOCK = threading.Lock()
LIVE_PROCESS = None
LIVE_PROCESS_LOCK = threading.Lock()


def run_command(args, timeout=8):
    started = time.time()
    try:
        proc = subprocess.run(
            args,
            cwd=PROJECT_ROOT,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "ok": proc.returncode == 0,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "elapsed_ms": int((time.time() - started) * 1000),
        }
    except subprocess.TimeoutExpired as exc:
        return {
            "ok": False,
            "returncode": 124,
            "stdout": exc.stdout or "",
            "stderr": "command timed out",
            "elapsed_ms": int((time.time() - started) * 1000),
        }


def file_state(path):
    p = Path(path)
    if not p.exists():
        return {"exists": False, "path": path}
    return {"exists": True, "path": path, "size": p.stat().st_size}


def collect_status():
    camera_nodes = ["/dev/video0", "/dev/media0", "/dev/v4l-subdev0"]
    return {
        "time": time.strftime("%Y-%m-%d %H:%M:%S"),
        "model": file_state(DEFAULT_MODEL),
        "input": file_state(DEFAULT_INPUT),
        "binary": file_state(str(PROJECT_ROOT / "build" / "cap_detect")),
        "camera": {node: Path(node).exists() for node in camera_nodes},
        "event_log": file_state(str(EVENT_LOG_PATH)),
        "latest": LATEST_RESULT,
    }


def cap_detect_command():
    command = [str(PROJECT_ROOT / "build" / "cap_detect"), "--model", DEFAULT_MODEL]
    if Path(DEFAULT_CAMERA).exists():
        command += [
            "--camera", DEFAULT_CAMERA,
            "--snapshot", SNAPSHOT_PATH,
            "--preview-gain", str(PREVIEW_GAIN),
            "--preview-gamma", str(PREVIEW_GAMMA),
            "--preview-clahe", str(PREVIEW_CLAHE),
            "--preview-awb-strength", str(PREVIEW_AWB_STRENGTH),
            "--preview-awb-red-bias", str(PREVIEW_AWB_RED_BIAS),
            "--preview-awb-blue-bias", str(PREVIEW_AWB_BLUE_BIAS),
            "--preview-saturation", str(PREVIEW_SATURATION),
            "--preview-hue-shift", str(PREVIEW_HUE_SHIFT),
            "--input-awb-strength", str(INPUT_AWB_STRENGTH),
            "--input-awb-red-bias", str(INPUT_AWB_RED_BIAS),
            "--input-awb-blue-bias", str(INPUT_AWB_BLUE_BIAS),
            "--input-saturation", str(INPUT_SATURATION),
            "--input-hue-shift", str(INPUT_HUE_SHIFT),
            "--input-gain", str(INPUT_GAIN),
            "--input-gamma", str(INPUT_GAMMA),
            "--input-clahe", str(INPUT_CLAHE),
            "--input-sharpen", str(INPUT_SHARPEN),
            "--jpeg-quality", str(JPEG_QUALITY),
        ]
    else:
        command += ["--input", DEFAULT_INPUT]
    command += ["--json"]
    return command


def run_detection_once():
    command = cap_detect_command()
    result = run_command(command, timeout=10)
    parsed = None
    error = None
    stdout = result["stdout"].strip()
    if stdout:
        try:
            start = stdout.find("{")
            end = stdout.rfind("}")
            if start < 0 or end < start:
                raise ValueError("no JSON object found in stdout")
            parsed = json.loads(stdout[start:end + 1])
        except (json.JSONDecodeError, ValueError) as exc:
            error = f"failed to parse cap_detect JSON: {exc}"
    else:
        error = "cap_detect produced empty stdout"

    payload = {
        "ok": result["ok"] and parsed is not None,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "command": " ".join(command),
        "stdout": result["stdout"],
        "stderr": result["stderr"],
        "returncode": result["returncode"],
        "elapsed_ms": result["elapsed_ms"],
        "parsed": parsed,
        "error": error,
    }
    record_detection_event(payload)
    return payload


def detections_from_result(result):
    parsed = result.get("parsed") if isinstance(result, dict) else None
    detections = parsed.get("detections") if isinstance(parsed, dict) else None
    return detections if isinstance(detections, list) else []


def record_detection_event(result):
    detections = detections_from_result(result)
    if not result.get("ok") or not detections:
        return
    parsed = result.get("parsed") or {}
    EVENT_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    event = {
        "timestamp": result.get("timestamp"),
        "snapshot": "/latest.jpg",
        "count": len(detections),
        "detections": detections,
        "raw_stats": parsed.get("raw_stats"),
    }
    with EVENT_LOG_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps(event, ensure_ascii=False) + "\n")


def read_events(limit=30):
    if not EVENT_LOG_PATH.exists():
        return []
    lines = EVENT_LOG_PATH.read_text(encoding="utf-8", errors="replace").splitlines()
    events = []
    for line in lines[-limit:]:
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return list(reversed(events))



def read_json_file(path):
    p = Path(path)
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None


def live_command():
    command = [
        str(PROJECT_ROOT / "build" / "cap_detect"),
        "--model", DEFAULT_MODEL,
        "--camera", DEFAULT_CAMERA,
        "--snapshot", SNAPSHOT_PATH,
        "--result", str(LIVE_RESULT_PATH),
        "--loop",
        "--interval-ms", str(int(LIVE_INTERVAL_SEC * 1000)),
        "--class-scale", str(CLASS_SCALE),
        "--preview-gain", str(PREVIEW_GAIN),
        "--preview-gamma", str(PREVIEW_GAMMA),
        "--preview-clahe", str(PREVIEW_CLAHE),
        "--preview-awb-strength", str(PREVIEW_AWB_STRENGTH),
        "--preview-awb-red-bias", str(PREVIEW_AWB_RED_BIAS),
        "--preview-awb-blue-bias", str(PREVIEW_AWB_BLUE_BIAS),
        "--preview-saturation", str(PREVIEW_SATURATION),
        "--preview-hue-shift", str(PREVIEW_HUE_SHIFT),
        "--input-awb-strength", str(INPUT_AWB_STRENGTH),
        "--input-awb-red-bias", str(INPUT_AWB_RED_BIAS),
        "--input-awb-blue-bias", str(INPUT_AWB_BLUE_BIAS),
        "--input-saturation", str(INPUT_SATURATION),
        "--input-hue-shift", str(INPUT_HUE_SHIFT),
        "--input-gain", str(INPUT_GAIN),
        "--input-gamma", str(INPUT_GAMMA),
        "--input-clahe", str(INPUT_CLAHE),
        "--input-sharpen", str(INPUT_SHARPEN),
        "--jpeg-quality", str(JPEG_QUALITY),
    ]
    return command


def live_process_running():
    with LIVE_PROCESS_LOCK:
        return LIVE_PROCESS is not None and LIVE_PROCESS.poll() is None


def start_live_process():
    global LIVE_PROCESS
    with LIVE_PROCESS_LOCK:
        if LIVE_PROCESS is not None and LIVE_PROCESS.poll() is None:
            return
        LIVE_RESULT_PATH.parent.mkdir(parents=True, exist_ok=True)
        LIVE_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        try:
            LIVE_RESULT_PATH.unlink()
        except FileNotFoundError:
            pass
        log = LIVE_LOG_PATH.open("ab", buffering=0)
        LIVE_PROCESS = subprocess.Popen(
            live_command(),
            cwd=PROJECT_ROOT,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )


def stop_live_process():
    global LIVE_PROCESS
    with LIVE_PROCESS_LOCK:
        proc = LIVE_PROCESS
        LIVE_PROCESS = None
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


def live_snapshot():
    parsed = read_json_file(LIVE_RESULT_PATH)
    running = live_process_running()
    result = None
    frame_id = 0
    last_update = None
    if parsed:
        frame_id = parsed.get("frame_id", 0)
        last_update = parsed.get("timestamp")
        result = {
            "ok": True,
            "timestamp": parsed.get("timestamp"),
            "command": " ".join(live_command()),
            "stdout": "",
            "stderr": "",
            "returncode": None,
            "elapsed_ms": parsed.get("elapsed_ms"),
            "parsed": parsed,
            "error": None,
        }
    error = None
    with LIVE_PROCESS_LOCK:
        if LIVE_PROCESS is not None and LIVE_PROCESS.poll() not in (None, 0):
            error = f"live process exited: {LIVE_PROCESS.returncode}"
    return {
        "running": running,
        "frame_id": frame_id,
        "last_update": last_update,
        "result": result,
        "error": error,
    }


def read_snapshot_bytes():
    try:
        return Path(SNAPSHOT_PATH).read_bytes()
    except OSError:
        return None


class Handler(SimpleHTTPRequestHandler):
    server_version = "CapDetectHTTP/0.1"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC_ROOT), **kwargs)

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args))

    def send_json(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self.send_json(collect_status())
            return
        if path == "/api/latest":
            self.send_json(LATEST_RESULT)
            return
        if path == "/api/live":
            self.send_json(live_snapshot())
            return
        if path == "/api/events":
            self.send_json({"ok": True, "events": read_events()})
            return
        if path == "/stream.mjpg":
            self.stream_mjpeg()
            return
        if path == "/":
            self.path = "/index.html"
        return super().do_GET()


    def stream_mjpeg(self):
        self.send_response(200)
        self.send_header("Age", "0")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.end_headers()
        delay = 1.0 / max(1.0, MJPEG_FPS)
        last_mtime = None
        last_body = None
        while True:
            try:
                p = Path(SNAPSHOT_PATH)
                body = None
                if p.exists():
                    mtime = p.stat().st_mtime_ns
                    if mtime != last_mtime:
                        body = p.read_bytes()
                        last_mtime = mtime
                        last_body = body
                    elif last_body is not None:
                        body = last_body
                if body:
                    self.wfile.write(b"--frame\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
                    self.wfile.write(body)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
                time.sleep(delay)
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                break
            except OSError:
                time.sleep(delay)

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api/run-test":
            self.run_test()
            return
        if path == "/api/camera-status":
            result = run_command(
                ["bash", "-lc", "ls -l /dev/video0 /dev/media0 /dev/v4l-subdev0 2>&1"],
                timeout=4,
            )
            self.send_json(result)
            return
        if path == "/api/start-live":
            self.start_live()
            return
        if path == "/api/stop-live":
            self.stop_live()
            return
        self.send_json({"ok": False, "error": "unknown endpoint"}, status=404)

    def run_test(self):
        global LATEST_RESULT
        LATEST_RESULT = run_detection_once()
        self.send_json(LATEST_RESULT)

    def start_live(self):
        start_live_process()
        self.send_json(live_snapshot())

    def stop_live(self):
        stop_live_process()
        self.send_json(live_snapshot())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    if not STATIC_ROOT.exists():
        raise SystemExit(f"static directory not found: {STATIC_ROOT}")
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"cap_detect web listening on http://{args.host}:{args.port}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
