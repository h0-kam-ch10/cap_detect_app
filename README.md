# A733 Cap Detection App

这是基于全志 A733 / Orange Pi 开发板的安全帽/目标检测原型项目。当前版本已经完成：

- IMX219 摄像头采集
- VIPLite NPU `.nb` 模型推理
- YOLOv8 输出后处理
- HTTP Web 控制台
- MJPEG 实时画面预览
- native 内部采集线程 + 推理线程解耦
- 检测框和实时推理结果展示
- 网页预览亮度增强

## 当前默认配置

板端项目目录：

```bash
/home/orangepi/cap_detect_app
```

开发主机项目目录：

```bash
/home/kamchio/a733_DEV/cap_detect_app
```

当前默认模型：

```bash
/opt/yolov8n/police_yolov8n_dual_uint8.nb
```

当前默认摄像头：

```bash
/dev/video0
```

当前 Web 地址：

```text
http://192.168.0.104:8080/
```

## 一键启动

在板端执行：

```bash
cd ~/cap_detect_app
./scripts/start_project.sh
```

该脚本会自动完成：停止旧服务、准备 IMX219 2-lane 摄像头、启动 Web、调用 `/api/start-live` 开始实时检测。启动完成后，按脚本输出的地址在浏览器打开即可看到实时检测画面。

停止/退出项目：

```bash
cd ~/cap_detect_app
./scripts/shutdown_project.sh
```

`shutdown_project.sh` 会先通过 Web API 请求 live 进程自行退出，再停止 `cap_detect`、Web 服务和 8080 端口占用。`stop_project.sh` 仍可使用，二者执行同一套关闭逻辑。native 进程正常退出时会释放采集线程、V4L2 mmap buffer 和进程内存；如果进程无响应，脚本会超时后强制结束，由内核回收资源。

## 启动项目

### 1. SSH 登录开发板

```bash
ssh orangepi@192.168.0.104
```

### 2. 准备 IMX219 摄像头

每次开发板重启后，如果 `/dev/video0` 不存在或摄像头无法打开，先执行：

```bash
cd ~/cap_detect_app
sudo ./scripts/board_prepare_camera.sh
```

该脚本会加载 `vin_io`、打补丁后的 `imx219` 2-lane 模块以及 `vin_v4l2`，并打印 `/dev/video0`、`/dev/media0` 等节点状态。

### 3. 编译 native 程序

通常只在修改 C++ 后需要执行：

```bash
cd ~/cap_detect_app
cmake --build build -j2
```

如果是首次配置构建目录：

```bash
cd ~/cap_detect_app
cmake -S native -B build
cmake --build build -j2
```

### 4. 启动 Web 服务

```bash
cd ~/cap_detect_app
./scripts/start_web.sh
```

默认监听：

```text
0.0.0.0:8080
```

如果端口被占用，可以先停止旧进程：

```bash
fuser -k 8080/tcp
```

然后重新启动。

### 5. 打开网页并启动 live

在开发主机浏览器打开：

```text
http://192.168.0.104:8080/
```

页面内点击 live/start 相关按钮即可启动实时检测。也可以直接用 API 启动：

```bash
curl -X POST http://192.168.0.104:8080/api/start-live
```

停止实时检测：

```bash
curl -X POST http://192.168.0.104:8080/api/stop-live
```

查看实时状态：

```bash
curl http://192.168.0.104:8080/api/live
```

## 常用环境变量

Web 服务支持通过环境变量覆盖默认参数：

```bash
CAP_MODEL=/opt/yolov8n/police_yolov8n_dual_uint8.nb CAP_CAMERA=/dev/video0 CAP_LIVE_INTERVAL_SEC=0.4 CAP_MJPEG_FPS=4 CAP_PREVIEW_GAIN=1.55 CAP_PREVIEW_GAMMA=0.72 CAP_PREVIEW_CLAHE=2.0 CAP_PREVIEW_AWB_STRENGTH=0.6 CAP_PREVIEW_AWB_RED_BIAS=0.78 CAP_PREVIEW_AWB_BLUE_BIAS=0.84 CAP_PREVIEW_SATURATION=1.55 CAP_PREVIEW_HUE_SHIFT=0 CAP_INPUT_AWB_STRENGTH=0.8 CAP_INPUT_AWB_RED_BIAS=0.84 CAP_INPUT_AWB_BLUE_BIAS=0.88 CAP_INPUT_SATURATION=1.25 CAP_INPUT_HUE_SHIFT=0 CAP_INPUT_GAIN=1.0 CAP_INPUT_GAMMA=0.92 CAP_INPUT_CLAHE=0 CAP_INPUT_SHARPEN=1.0 CAP_JPEG_QUALITY=72 ./scripts/start_web.sh
```

关键参数说明：

- `CAP_MODEL`：板端 `.nb` 模型路径。
- `CAP_CAMERA`：摄像头节点，默认 `/dev/video0`。
- `CAP_LIVE_INTERVAL_SEC`：live 推理循环间隔，默认 `0.4`，约 2-4 FPS 检测节奏，优先保证 A733/VIN 稳定。
- `CAP_MJPEG_FPS`：网页 MJPEG 输出帧率，默认 `4`。
- `CAP_PREVIEW_GAIN`：只增强网页预览亮度，不改变模型输入，默认 `1.55`。
- `CAP_PREVIEW_GAMMA`：预览 gamma 校正，默认 `0.72`，小于 1 会提亮暗部。
- `CAP_PREVIEW_CLAHE`：预览局部对比度增强，默认 `2.0`，设为 `0` 可关闭。
- `CAP_PREVIEW_AWB_STRENGTH`：预览白平衡修正强度，默认 `0.6`，用于压住 IMX219 当前偏红画面，只影响 HTML/MJPEG 预览。
- `CAP_PREVIEW_AWB_RED_BIAS`：预览红通道额外修正系数，默认 `0.66`，越小越去红。
- `CAP_PREVIEW_AWB_BLUE_BIAS`：预览蓝通道额外修正系数，默认 `0.70`，当前用于降低摄像头偏蓝/偏紫。
- `CAP_PREVIEW_SATURATION`：预览 HSV 饱和度倍率，默认 `1.55`，参考 `test.png` 提升摄像头低饱和画面。
- `CAP_PREVIEW_HUE_SHIFT`：预览 HSV 色相偏移角度，默认 `0`，需要微调色相时再启用。
- `CAP_INPUT_AWB_STRENGTH`：模型输入白平衡修正强度，默认 `0.8`，用于修正 IMX219 偏红/偏蓝。
- `CAP_INPUT_AWB_RED_BIAS`：模型输入红通道修正系数，默认 `0.74`。
- `CAP_INPUT_AWB_BLUE_BIAS`：模型输入蓝通道修正系数，默认 `0.78`。
- `CAP_INPUT_SATURATION`：模型输入 HSV 饱和度倍率，默认 `1.25`，比预览更保守，避免过度改变训练分布。
- `CAP_INPUT_HUE_SHIFT`：模型输入 HSV 色相偏移角度，默认 `0`。
- `CAP_INPUT_GAIN`：模型输入曝光/亮度增益，默认 `1.0`。
- `CAP_INPUT_GAMMA`：模型输入 gamma，默认 `0.92`，轻微提亮暗部。
- `CAP_INPUT_CLAHE`：模型输入局部对比度增强，默认 `0`，避免过强改变训练分布。
- `CAP_INPUT_SHARPEN`：模型输入轻微锐化，默认 `0.6`，用于补偿 IMX219 当前画面偏糊。
- `CAP_JPEG_QUALITY`：预览 JPEG 质量，默认 `72`，用于降低编码和传输压力。
- `CAP_CLASS_SCALE`：后处理类别分数缩放，当前默认 `1`。

## 单次命令测试

测试固定输入 tensor：

```bash
cd ~/cap_detect_app
./build/cap_detect   --model /opt/yolov8n/police_yolov8n_dual_uint8.nb   --input /opt/yolov8n/traffic_police_01_chw_rgb_320.dat   --result /tmp/test.json   --json
```

测试摄像头单帧：

```bash
cd ~/cap_detect_app
./build/cap_detect   --model /opt/yolov8n/police_yolov8n_dual_uint8.nb   --camera /dev/video0   --snapshot /tmp/camera.jpg   --result /tmp/camera.json   --preview-gain 1.55   --preview-gamma 0.72   --preview-clahe 2.0   --preview-awb-strength 0.6   --preview-awb-red-bias 0.66   --preview-awb-blue-bias 0.70   --preview-saturation 1.55   --preview-hue-shift 0   --input-awb-strength 0.8   --input-awb-red-bias 0.74   --input-awb-blue-bias 0.78   --input-saturation 1.25   --input-hue-shift 0   --input-gain 1.0   --input-gamma 0.92   --input-clahe 0   --input-sharpen 0.6   --jpeg-quality 72   --json
```

## 当前线程结构

当前 native live 模式已经拆分为：

```text
采集线程：持续持有 /dev/video0 并更新最新帧
推理线程：按 live interval 取最新帧做 NPU 推理
Web 端：暂时继续读取 latest.jpg/latest.json
```

下一步如果继续提升网页画面帧率，应把 `latest.jpg` 文件轮询改为内存 MJPEG 或 socket/WebSocket 流。

## 输出文件

Web live 模式会持续更新：

```text
~/cap_detect_app/web/static/latest.jpg
~/cap_detect_app/web/static/latest.json
```

MJPEG 流地址：

```text
http://192.168.0.104:8080/stream.mjpg
```

事件日志默认写入：

```text
~/cap_detect_app/data/events.jsonl
```
