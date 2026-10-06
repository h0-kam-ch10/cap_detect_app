# 2026-07-09 项目进度记录

## 当前状态

项目主体已经可以在 A733 / Orange Pi 开发板上完成“摄像头采集 -> YOLOv8 NPU 推理 -> Web 页面实时查看”的闭环。当前为了对比原始摄像头画面，Web/live 服务已暂停，`/dev/video0` 已释放。

最近一次原始 demo 截图基于 `/opt/v4l2_opencv_demo` 的 C++ 采集代码完成，未做项目内的亮度、gamma、CLAHE 增强：

- 本机文件：`cap_detect_app/demo_raw_camera_20260709.jpg`
- 板端文件：`/tmp/demo_raw_camera.jpg`
- 分辨率：`640x480`
- JPEG quality：`95`

## 已实现功能

### 1. 模型转换与部署

已将训练得到的双输出 YOLOv8 ONNX 模型通过 Pegasus 转换为 VIPLite `.nb`，并部署到板端。

当前主要模型文件：

- ONNX：`/home/kamchio/a733_DEV/yolov8n/police_dual_20260708/police_yolov8n_320_dual_output.onnx`
- 本机 NB：`/home/kamchio/a733_DEV/docker_images_v2.0.x/docker_data/ai-sdk/models/police_yolov8n_dual/police_yolov8n_dual_uint8.nb`
- 板端 NB：`/opt/yolov8n/police_yolov8n_dual_uint8.nb`

实现对应代码：

- `native/npu_runner/npu_runner.cpp`：封装 VIPLite 推理，加载 `.nb`，执行 NPU，读取输出。
- `native/third_party/awnn_viplite/awnn_lib.c`：AWNN/VIPLite 底层封装，支持查询输出数量和元素数量。
- `native/third_party/awnn_viplite/awnn_lib.h`：导出 `awnn_get_output_count()`、`awnn_get_output_elements()` 等接口。
- `native/CMakeLists.txt`：链接 VIPLite / AWNN 相关代码。

### 2. 单输出与双输出 YOLOv8 适配

当前 `cap_detect` 已同时兼容：

- 单输出布局：`[1, 6, 2100]`
- 双输出布局：bbox `[1, 4, 2100]` + class `[1, 2, 2100]`

双输出情况下，程序会根据 tensor 元素数量识别 bbox/class 输出顺序，然后合并为后处理统一使用的 12600 个 float。

实现对应代码：

- `native/npu_runner/npu_runner.cpp`：`NpuRunner::run_tensor_data()` 中识别输出数量、输出元素数，并合并双输出 tensor。
- `native/postprocess/yolov8_post.cpp`：YOLOv8 后处理、阈值筛选、NMS。
- `native/postprocess/yolov8_post.h`：检测框结构和后处理配置。

### 3. 摄像头驱动准备脚本

已实现 IMX219 摄像头准备脚本，用于每次板端重启后加载可用的 2-lane 摄像头链路。

实现对应代码：

- `scripts/board_prepare_camera.sh`

脚本主要功能：

- 读取当前内核版本。
- 从系统 `imx219.ko` 生成 `/tmp/imx219_2lane.ko`。
- 通过二进制 patch 将 IMX219 从 4-lane 改为 2-lane。
- 卸载旧的 `vin_v4l2`、`ov13850_mipi`、`imx219`、`vin_io`。
- 重新加载 `vin_io`、patch 后的 `imx219_2lane.ko` 和 `vin_v4l2`。
- 打印 `/dev/video0`、`/dev/media0`、`/dev/v4l-subdev*` 节点和 media topology。

注意事项：

- 不要使用 `v4l2-ctl --all` 做全量探测，之前触发过 `vin_v4l2` 内核 NULL pointer/Oops。
- 如果出现 `isp0 configuration error` / `isp0 width error` 并持续 `select timeout`，通常是 VIN/ISP 管线卡死，重启开发板后重新运行脚本可以恢复。

### 4. V4L2 摄像头采集

已实现项目内的 V4L2 多平面采集。当前实际采集格式为：

- 设备：`/dev/video0`
- 分辨率：`640x480`
- V4L2 buffer type：`V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`
- 像素格式：`YM12` / `YUV420M`
- plane：Y/U/V 三平面

实现对应代码：

- `native/camera/camera_v4l2.h`：`V4L2Camera` 类定义。
- `native/camera/camera_v4l2.cpp`：打开设备、设置格式、申请 mmap buffer、QBUF/DQBUF、YUV420M 转 BGR。
- `native/camera/camera_v4l2.cpp`：`bgr_to_chw_rgb_320()` 将摄像头 BGR 图转为模型输入 CHW RGB 320x320。

已加入调试开关：

```bash
CAP_CAMERA_DEBUG=1 ./build/cap_detect ...
```

启用后会打印打开设备、实际格式、buffer、DQBUF bytesused 等信息。默认不刷详细日志。

### 5. 模型输入预处理

摄像头帧会被转换成 YOLOv8 模型需要的 320x320 CHW RGB tensor。

实现对应代码：

- `native/camera/camera_v4l2.cpp`：`bgr_to_chw_rgb_320()`。

处理逻辑：

- BGR 转 RGB。
- 按比例 resize。
- letterbox 到 320x320。
- 输出 CHW RGB uint8 tensor。

### 6. Native 命令行程序 `cap_detect`

已实现统一命令行程序，支持：

- 固定 tensor 输入测试。
- 摄像头单帧测试。
- 摄像头循环 live 模式。
- 输出 JSON 结果。
- 保存预览图 `latest.jpg`。
- 保存检测结果 `latest.json`。
- 配置推理间隔、类别分数缩放、预览增强参数。

实现对应代码：

- `native/app/main.cpp`

主要参数：

```bash
--model /opt/yolov8n/police_yolov8n_dual_uint8.nb
--input /opt/yolov8n/traffic_police_01_chw_rgb_320.dat
--camera /dev/video0
--snapshot /tmp/camera.jpg
--result /tmp/camera.json
--loop
--interval-ms 400
--class-scale 1
--preview-gain 1.4
--preview-gamma 0.75
--preview-clahe 2.0
--jpeg-quality 72
--json
```

### 7. 网页控制台

已实现可通过浏览器访问的 HTML 控制台，用于查看模型、摄像头、推理状态和实时画面。

访问地址示例：

```text
http://192.168.0.104:8080/
```

实现对应代码：

- `web/server.py`：HTTP API、Web 静态文件服务、live 子进程管理、MJPEG 推流。
- `web/static/index.html`：页面结构。
- `web/static/app.js`：前端状态轮询、启动/停止 live、绘制检测框、显示检测结果。
- `web/static/styles.css`：页面样式。
- `scripts/start_web.sh`：启动 Web 服务。

已实现 API：

- `GET /api/status`：模型、二进制、摄像头节点状态。
- `POST /api/run-test`：运行一次推理测试。
- `POST /api/start-live`：启动实时检测子进程。
- `POST /api/stop-live`：停止实时检测子进程。
- `GET /api/live`：读取实时状态和最新 JSON。
- `GET /api/events`：读取事件日志。
- `GET /stream.mjpg`：MJPEG 实时流。

### 8. MJPEG 实时预览与检测框显示

已将早期的 `latest.jpg` 轮询方式改为 MJPEG 流，前端使用 `<img>` 连接 `/stream.mjpg`。检测框由前端 canvas 根据 `latest.json` 绘制。

实现对应代码：

- `web/server.py`：`stream_mjpeg()` 从 `latest.jpg` 读取并输出 multipart MJPEG。
- `web/static/app.js`：`ensureStream()` 连接 MJPEG；`drawDetections()` 绘制检测框。
- `native/app/main.cpp`：loop 模式持续写入 `latest.jpg` 和 `latest.json`。

### 9. 预览画面亮度/对比度优化

已实现只作用于网页预览图的增强，不影响模型输入 tensor。

当前默认参数：

```bash
CAP_PREVIEW_GAIN=1.4
CAP_PREVIEW_GAMMA=0.75
CAP_PREVIEW_CLAHE=2.0
CAP_JPEG_QUALITY=72
```

实现对应代码：

- `native/app/main.cpp`：`enhance_preview_rgb()`。
- `native/app/main.cpp`：`run_camera_frame()` 中先生成模型 tensor，再单独对 preview RGB 做 gain/gamma/CLAHE/JPEG quality。
- `web/server.py`：读取环境变量并将参数传给 `cap_detect`。

说明：

- `preview_gain`：整体亮度增益。
- `preview_gamma < 1`：提亮暗部。
- `preview_clahe`：增强局部对比度。
- `jpeg_quality`：降低 JPEG 编码和网络传输压力。

### 10. 稳定性调试与保守实时参数

由于 A733 当前 VIN/ISP 管线偶发卡死，实时参数暂时不追求高帧率，而是优先稳定。

当前建议默认：

```bash
CAP_LIVE_INTERVAL_SEC=0.4
CAP_MJPEG_FPS=4
```

实测结果：

- `frame_id` 每秒增长约 2-3。
- 实际检测/刷新约 2.5 FPS。
- `cap_detect` CPU 约 20%。
- Web 服务 CPU 约 3%-4%。

实现对应代码：

- `web/server.py`：`LIVE_INTERVAL_SEC`、`MJPEG_FPS` 默认值。
- `web/server.py`：`live_command()` 拼接 `--interval-ms`。
- `native/app/main.cpp`：`run_loop()` 根据每帧耗时 sleep。

### 11. 原始 demo 摄像头截图对比

已暂停项目，并基于 `/opt/v4l2_opencv_demo` 的 C++ 采集类完成原始截图，便于对比项目增强前后的画面。

使用的 demo 代码：

- `/opt/v4l2_opencv_demo/v4l2_camera.cpp`
- `/opt/v4l2_opencv_demo/v4l2_camera.h`

临时编译的 headless 程序：

- 板端源码：`/tmp/demo_headless_capture.cpp`
- 板端可执行：`/tmp/demo_headless_capture`

输出：

- 板端：`/tmp/demo_raw_camera.jpg`
- 本机：`cap_detect_app/demo_raw_camera_20260709.jpg`

## 当前未完成 / 待实现功能

### 1. 摄像头链路自动恢复

现状：

- 如果 VIN/ISP 管线卡死，表现为 `STREAMON` 成功但 `select timeout`。
- 内核日志可能出现 `isp0 configuration error` / `isp0 width error`。
- 当前有效恢复方式是重启开发板后重新运行 `board_prepare_camera.sh`。

待实现：

- 在 Web 或守护脚本中检测 live 长时间无新帧。
- 自动停止 live。
- 自动尝试重跑 `board_prepare_camera.sh`。
- 如果仍失败，提示需要人工重启开发板。

### 2. 摄像头 2-lane 初始化自动化

现状：

- 每次重启后需要手动执行：

```bash
cd /home/orangepi/cap_detect_app
sudo ./scripts/board_prepare_camera.sh
```

待实现：

- 增加 systemd service，在开机后自动执行 `board_prepare_camera.sh`。
- 移除或避免 `/etc/modules-load.d/` 中自动加载原始未 patch 的 `imx219`。

### 3. 更高帧率架构

现状：

- 当前 live 仍由 `cap_detect --loop` 采集、推理、写 JPEG、写 JSON。
- Web MJPEG 再从 `latest.jpg` 读文件推流。
- 文件 I/O 和 JPEG 编码仍是瓶颈之一。

待实现：

- 采集线程、推理线程、Web 推流解耦。
- 最新帧保存在内存中，而不是每帧写文件后再读文件。
- MJPEG 直接从内存帧输出。
- 推理可以低频，画面可以高频。

目标：

- 稳定预览 5-10 FPS。
- 检测 2-5 FPS。
- 在 A733 当前稳定性约束下，不建议直接冲 30 FPS。

### 4. 检测精度继续优化

现状：

- 固定 tensor 测试模型可以输出目标结果。
- 摄像头实拍时仍可能检测不稳定，尤其受光照、距离、角度、目标尺寸影响。

待实现：

- 收集更多 A733 实拍图片作为训练/校准数据。
- 增加暗光、逆光、模糊、小目标、不同姿态 traffic police 样本。
- 重新训练或 fine-tune。
- 重新导出双输出 ONNX。
- 重新 Pegasus 转 `.nb` 并用板端实拍 tensor 做量化校准。
- 根据实际误检/漏检调整后处理阈值和 NMS。

### 5. Web 页面可调参数

现状：

- 增强参数通过环境变量配置。
- 页面上不能直接调 `gain/gamma/CLAHE/JPEG quality/FPS`。

待实现：

- 在 Web 页面增加参数控件。
- 支持修改后重启 live。
- 显示当前 live 命令参数和实际 FPS。

### 6. 事件业务逻辑

现状：

- 已有事件日志写入能力，但业务规则还较基础。
- 当前只是检测到目标时记录事件。

待实现：

- 按 `cap_detect_plan.md` 继续实现项目级业务逻辑。
- 加入安全帽/人员/交通警察等实际业务类别后，定义事件触发规则。
- 支持事件图片归档、时间戳、置信度、类别、区域等字段。
- 支持事件去重和冷却时间，避免同一目标持续刷日志。

### 7. 文档与运维完善

待实现：

- 将模型转换全过程、板端部署、常见故障恢复、启动命令整理成最终版文档。
- 明确禁止使用的危险调试命令，例如 `v4l2-ctl --all`。
- 增加一键启动脚本：准备摄像头、启动 Web、检查 live 状态。
- 增加一键停止脚本：停止 live、停止 Web、释放摄像头。

## 当前推荐启动流程

项目暂停后，如需重新启动测试：

```bash
ssh orangepi@192.168.0.104
cd /home/orangepi/cap_detect_app
sudo ./scripts/board_prepare_camera.sh
CAP_MODEL=/opt/yolov8n/police_yolov8n_dual_uint8.nb CAP_CAMERA=/dev/video0 ./scripts/start_web.sh
```

浏览器访问：

```text
http://192.168.0.104:8080/
```

如果只做单帧测试：

```bash
cd /home/orangepi/cap_detect_app
./build/cap_detect   --model /opt/yolov8n/police_yolov8n_dual_uint8.nb   --camera /dev/video0   --snapshot /tmp/camera.jpg   --result /tmp/camera.json   --preview-gain 1.4   --preview-gamma 0.75   --preview-clahe 2.0   --jpeg-quality 72   --json
```

## 已知风险

- A733 当前 VIN/ISP 驱动稳定性有限，持续采集可能触发 `select timeout`。
- `v4l2-ctl --all` 曾触发内核 Oops，应避免使用。
- 重启后必须确保加载的是 patch 后的 2-lane IMX219 模块。
- 当前 Web/live 已暂停，恢复测试前需要重新启动服务。
