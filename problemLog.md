# Problem Log

本文记录从 YOLOv8n ONNX 模型转换、全志 A733 板端部署到 Web 实时检测过程中遇到的主要问题和最终解决方式，便于后续复现和排查。

## 1. IMX219 摄像头驱动默认不可用

### 现象

- `/dev/video0` 不稳定或不存在。
- 摄像头程序无法打开设备。
- 开发板重启后摄像头需要重新准备。

### 原因

板端 IMX219 模块默认配置和当前硬件连接不完全匹配，需要把驱动里的 4-lane 配置改成 2-lane 配置。

### 解决方法

使用项目脚本：

```bash
cd ~/cap_detect_app
sudo ./scripts/board_prepare_camera.sh
```

脚本会把：

```text
01038052
```

替换为：

```text
41028052
```

并加载：

```text
vin_io
imx219 patched module
vin_v4l2
```

## 2. `/var/log` 容易被内核日志写满

### 现象

- 开发板运行一段时间后异常卡顿。
- `/var/log` 所在 zram 空间接近满。

### 处理方法

必要时清理日志：

```bash
printf "HJKL;\047\n" | sudo -S sh -c 'find /var/log -type f -exec truncate -s 0 {} +'
```

后续应尽量避免长时间保留高频内核日志。

## 3. 摄像头 YUV 画面颜色异常

### 现象

网页上的摄像头画面偏灰、偏暗，颜色不正常。

### 原因

最初使用 `YM12` / non-contiguous YUV 格式，并按普通 I420 方式解码，容易导致颜色不对。

### 解决方法

改为让 V4L2 请求：

```text
V4L2_PIX_FMT_BGR24 / BGR3
640x480
```

程序直接按 packed BGR 读取，再转 RGB 做 letterbox 和模型输入。

验证格式：

```bash
v4l2-ctl --device=/dev/video0 --get-fmt-video
```

期望看到：

```text
Pixel Format      : 'BGR3' (24-bit BGR 8-8-8)
Width/Height      : 640/480
```

## 4. 网页实时画面帧率低

### 现象

最初网页通过轮询 `latest.jpg` 刷新，每帧 HTTP 请求开销较大，画面不够流畅。

### 解决方法

改为 MJPEG 流：

```text
/stream.mjpg
```

前端使用 `<img>` 显示 MJPEG，检测框由 canvas 覆盖绘制。实时 JSON 元数据仍通过 `/api/live` 轮询。

当前保守参数：

```text
CAP_LIVE_INTERVAL_SEC=0.1
CAP_MJPEG_FPS=10
```

这样对 A733 负载较稳，不容易过载。

## 5. 每帧启动推理程序导致负载过高

### 现象

早期方案每一帧都启动一次 `cap_detect`，资源开销大，开发板可能过载甚至关机。

### 解决方法

`cap_detect` 增加 loop 模式：

```bash
--loop --interval-ms 100
```

Web 服务启动一个常驻 native 进程，循环读取摄像头、推理、写入 `latest.jpg` 和 `latest.json`。

## 6. `latest.jpg` 偶发读到半写入文件

### 现象

浏览器或 Python 拉取 `latest.jpg` 时偶发 JPEG 解析失败。

### 原因

native 程序正在写 `latest.jpg`，Web/MJPEG 同时读取，可能读到半写入文件。

### 解决方法

改成原子写入：

1. 先写 `latest.jpg.tmp.jpg`
2. 写完后 `rename()` 覆盖 `latest.jpg`

注意临时文件必须保留 `.jpg` 扩展，否则 OpenCV `imwrite()` 找不到 JPEG writer。

## 7. 原始 `uint8.nb` 模型无法检测目标

### 现象

固定测试图和摄像头实时画面都没有检测框。`latest.json` 中：

```json
"max_class_score": 0.0,
"channels": [
  {"max": 318.9},
  {"max": 310.1},
  {"max": 159.4},
  {"max": 253.6},
  {"max": 0.0},
  {"max": 0.0}
]
```

### 原因

YOLOv8 输出 tensor 是 `[bbox(0~320), class(0~1)]` 拼在同一个 `[1,6,2100]` 输出里。Pegasus 转 `uint8.nb` 后，输出量化尺度被 bbox 大范围主导，class 通道被压成 0。

ONNX Runtime 验证原 ONNX 类别输出正常：

```text
class max approximately 0.61
```

因此问题不是训练模型，也不是后处理，而是 `.onnx -> .nb` 量化后的输出尺度问题。

## 8. `bf16.nb` 尝试失败

### 现象

尝试导出 `bf16` 模型时，先后遇到：

- 缺 `jpeglib.h`
- 缺 `vsi_nn_pub.h`
- 缺 `libjpeg.a`
- `gen_nbg` 运行时找不到 `libvdtproxy.so`
- shader 编译失败，生成 0 字节 `network_binary.nb`

### 处理过程

补齐过这些 host 侧 SDK 内容：

- Vivante IDE x86 simulator libs
- `unified-android/include/acuity-ovxlib-dev`
- JPEG headers / `libjpeg.a`
- `cl_viv_vx_ext.h`

但最终 `bf16` shader 编译仍失败，未得到可用 `.nb`。

### 结论

当前工具链下 `bf16` 不作为主线方案。

## 9. `int16.nb` 能运行但类别分数过低

### 现象

`int16.nb` 可在板端加载运行，但类别最大值只有：

```text
max_class_score approximately 0.0156
```

仍低于默认阈值，不能可靠出框。

### 结论

`int16` 解决了部分量化为 0 的问题，但精度仍不足。

## 10. `pcq.nb` 仍然类别通道为 0

### 现象

per-channel quantization (`pcq`) 导出成功，但板端测试类别通道仍全 0。

### 结论

Pegasus 最终输出 tensor 仍然受到同一输出尺度影响，`pcq` 没能解决 final concat 的 bbox/class 混合量化问题。

## 11. `.nb` target 不匹配

### 现象

板端加载新 `.nb` 时报错：

```text
binary target=0x10000020, actually target=0x1000003B
fail to create network status=-4
```

### 原因

导出时用了旧参数 `t736`，生成的 NBG target 与 A733 实际 VIP target 不一致。

### 解决方法

导出时使用：

```text
VIP9000NANODI_PLUS_PID0X1000003B
```

对应脚本：

```bash
source ../scripts/pegasus_setup.sh v3
../scripts/pegasus_export_ovx_nbg.sh yolov8n_scaled uint8 VIP9000NANODI_PLUS_PID0X1000003B ../viplite-android/v2.0
```

## 12. 最终可用模型方案：输出 class 放大版 ONNX

### 思路

在 ONNX 最后输出前，将 class 分支从：

```text
sigmoid_class
```

改成：

```text
sigmoid_class * 320
```

这样 class 和 bbox 处于接近量级，避免量化时被 bbox 范围压没。

ONNX 尾部原结构：

```text
Concat([bbox, sigmoid_class]) -> output0
```

改成：

```text
Mul(sigmoid_class, 320) -> scaled_class
Concat([bbox, scaled_class]) -> output0
```

### 验证

ONNX Runtime 对比：

```text
原 ONNX class max: approximately 0.61
scaled ONNX class max: approximately 195
```

板端 `.nb` 固定输入测试：

- 类别通道不再全 0
- 可以输出检测框

当前部署模型：

```text
/opt/yolov8n/yolov8n_scaled_uint8.nb
```

当前 Web 默认使用该模型。

## 13. scaled 模型的注意事项

当前 scaled 模型能检测出目标，但板端类别分数被量化成较粗的一档，固定测试图中出现过：

```text
score: 1.2559
```

因此当前分数更适合作为“是否有目标”的触发信号，不适合直接当成精细置信度解释。后续更严谨的方案是重新导出多输出 ONNX，让 bbox 和 class 分成两个输出 tensor，从根上避免混合量化尺度。

## 14. 画面亮度提升

### 现象

V4L2 控制项调高后，实际画面亮度几乎没有变化：

```text
brightness / contrast / saturation / auto_exposure_bias
```

对当前 IMX219/ISP 链路效果有限。

### 解决方法

在 native 程序中增加预览增益：

```bash
--preview-gain 2.5
```

Web 默认：

```text
CAP_PREVIEW_GAIN=2.5
```

该参数只增强写给网页的 `latest.jpg`，不改变送入模型的 tensor，避免影响检测输入分布。

亮度统计从约 `8%` 提升到约 `21%`。

## 15. 当前稳定启动命令

板端启动：

```bash
cd ~/cap_detect_app
sudo ./scripts/board_prepare_camera.sh
cmake --build build -j2
./scripts/start_web.sh
```

开发主机浏览器访问：

```text
http://192.168.0.102:8080/
```

API 启动 live：

```bash
curl -X POST http://192.168.0.102:8080/api/start-live
```

当前 live 实际执行命令形态：

```bash
~/cap_detect_app/build/cap_detect   --model /opt/yolov8n/yolov8n_scaled_uint8.nb   --camera /dev/video0   --snapshot ~/cap_detect_app/web/static/latest.jpg   --result ~/cap_detect_app/web/static/latest.json   --loop   --interval-ms 100   --class-scale 1   --preview-gain 2.5
```
