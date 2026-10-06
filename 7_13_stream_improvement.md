# 2026-07-13 实时画面帧率优化方向

## 目标

在不影响当前实时目标检测效果的前提下，提高 HTML 页面中的摄像头实时画面流畅度。

核心原则：

```text
检测输入链路保持不变
只优化采集、预览、传输、调度
```

当前检测效果依赖以下输入校正参数，优化帧率时应优先保持不变：

```bash
--input-awb-strength 0.8
--input-awb-red-bias 0.92
--input-awb-blue-bias 0.98
--input-gain 1.0
--input-gamma 0.95
--input-clahe 0
--input-sharpen 0.6
```

## 当前链路

当前 `cap_detect --loop` 基本是串行流程：

```text
摄像头取帧 -> 预处理 -> NPU 推理 -> 后处理 -> 写 latest.jpg/json -> sleep
```

Web 端 MJPEG 流再从 `latest.jpg` 读取图像：

```text
cap_detect 写 latest.jpg
web/server.py 读取 latest.jpg
浏览器通过 /stream.mjpg 查看画面
```

这个结构简单可靠，但有几个限制：

- 画面帧率被推理循环节奏限制。
- JPEG 编码和文件 I/O 会阻塞整个 loop。
- Web 端反复 `stat/read latest.jpg` 有额外开销。
- 推理慢时，画面也跟着卡。
- 如果后续提高 MJPEG FPS，文件读写会更容易成为瓶颈。

## 优化方向

### 1. 采集、推理、推流解耦

推荐优先实现。

当前串行链路应改为：

```text
camera thread: 持续采集最新帧
inference thread: 按固定间隔取最新帧推理
web/mjpeg thread: 推送最新预览帧
```

目标效果：

- 检测仍保持当前稳定节奏，例如 2-3 FPS。
- 网页画面可以提升到 8-15 FPS。
- 推理耗时不会直接阻塞摄像头采集。
- 浏览器画面延迟会降低。

涉及代码：

- `native/app/main.cpp`
- `native/camera/camera_v4l2.cpp`
- `web/server.py`

关键原则：

```text
推理线程只拿最新帧
旧帧直接丢弃
不要排队处理历史帧
```

### 2. MJPEG 改为内存帧，减少 latest.jpg 文件 I/O

当前结构：

```text
cap_detect -> latest.jpg -> web/server.py -> /stream.mjpg
```

更优结构：

```text
cap_detect/latest frame producer -> memory buffer -> MJPEG response
```

可选实现：

1. `cap_detect` 自己提供 MJPEG HTTP 输出。
2. `cap_detect` 通过 socket/stdout/shared memory 把 JPEG 帧交给 `web/server.py`。
3. `web/server.py` 维护内存中的 latest_jpeg，不再每帧读磁盘。

保守过渡方案：

- JSON 检测结果继续写 `latest.json`。
- 预览帧先减少磁盘读写频率。
- 稳定后再完全去掉 `latest.jpg` 轮询。

### 3. 预览帧和检测帧分辨率分开

当前检测输入是 320x320，模型依赖这个输入尺寸和 letterbox 逻辑。

建议保持：

```text
检测输入：320x320，不改变
网页预览：可使用 640x480、512x384 或 480x360
```

这样可以：

- 保持检测效果不变。
- 网页画面更自然。
- 允许按 CPU 压力选择预览分辨率。

注意：

- 不要改变模型输入尺寸。
- 不要改变当前检测输入的白平衡、gamma、锐化和 letterbox 逻辑。
- 预览增强和模型输入增强仍应分开控制。

### 4. JPEG 编码单独线程化

JPEG 编码会消耗 CPU。当前如果放在推理 loop 中，会影响整体节奏。

建议改成：

```text
capture thread -> latest_bgr/latest_rgb
jpeg thread -> latest_jpeg
inference thread -> latest_detection
```

这样可以：

- 避免 JPEG 编码阻塞 NPU 推理。
- Web 端可以更稳定地拿到最新 JPEG。
- 推理线程只关注模型输入和 NPU。

### 5. 保持 NPU 单 worker

A733 当前 NPU 日志显示：

```text
device_cnt=1, core_cnt=1
set core(index): 0, percent: 100, freq: 1008000000
```

因此不建议开多个 NPU 推理线程。

合理结构是：

```text
采集线程：1 个
推理线程：1 个
JPEG/推流线程：1 个或由 Web 请求线程承担
事件/日志线程：可选
NPU worker：1 个
```

不要像 RK3588 那样尝试多 NPU core 并行，因为 A733 当前只暴露 1 个 NPU core。

## 2026-07-13 阶段 1 已实施

已在 `native/app/main.cpp` 中完成低风险解耦：

```text
采集线程：独立打开并持续持有 /dev/video0，循环获取最新帧
推理线程：主 loop 等待最新帧，按现有 interval 做 NPU 推理和后处理
Web 输出：保持 latest.jpg/latest.json 不变
```

关键实现点：

- 新增 `SharedFrame`，用 `mutex + condition_variable` 保存最新摄像头帧。
- 新增 `capture_loop()`，摄像头只在采集线程中初始化、start、取帧。
- 新增 `run_rgb_frame()`，把原来“已拿到一帧后的预处理/JPEG/NPU”逻辑复用出来。
- `run_loop()` 不再直接阻塞在摄像头采集上，而是等待采集线程更新的新帧。
- `latest.jpg` 和 `latest.json` 写入格式保持兼容，Web 端暂不改。

当前阶段的收益主要是让摄像头采集不被 NPU 推理和 JPEG 写文件直接阻塞。HTML 画面仍受 `latest.jpg` 文件更新频率限制，真正提升到更高预览 FPS 还需要阶段 2/3。

## 推荐实施顺序

### 阶段 1：低风险解耦

先改 `cap_detect` 内部结构：

```text
camera capture thread
inference loop thread/main loop
```

保留现有输出：

```text
latest.jpg
latest.json
```

优点：

- 对 Web 改动小。
- 检测输出格式不变。
- 一键启动脚本基本不需要改。
- 可以先验证实时画面和检测稳定性。

### 阶段 2：增加预览帧缓存

在 native 侧维护最新预览帧：

```text
latest_preview_rgb/latest_preview_jpeg
```

让 JPEG 编码从推理 loop 中拆出来。

### 阶段 3：Web 内存流或 socket 流

把 Web MJPEG 从文件读取改为内存或 socket：

```text
native producer -> web memory cache -> /stream.mjpg
```

目标：

- 减少文件 I/O。
- 提高 MJPEG FPS。
- 降低延迟。

### 阶段 4：Web 参数面板和 FPS 显示

增加页面可调参数：

- live interval
- MJPEG FPS
- preview gain/gamma/CLAHE/AWB
- input AWB/gamma/sharpen
- JPEG quality

增加显示：

- 实际推理 FPS
- 实际 MJPEG FPS
- 最新 frame_id
- NPU elapsed

## 当前建议目标

在 A733 当前稳定性约束下，不建议直接追求 30 FPS。

更合理的近期目标：

```text
网页预览：5-10 FPS
目标检测：2-5 FPS
端到端延迟：尽量小于 500ms-1s
```

当前最值得先做的是：

```text
采集线程和推理线程解耦
```

这是对检测效果影响最小、对画面卡顿改善最明显的第一步。
