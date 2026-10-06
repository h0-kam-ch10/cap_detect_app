# 基于 YOLOv8n 的 A733 实时目标检测项目计划

本文档描述一个在 Orange Pi 4 Pro A733 开发板上运行的实时目标检测项目。当前 YOLOv8n ONNX 模型已经通过 Acuity/Pegasus 工具链转换为 A733 NPU 可运行的 `.nb` 模型，并已在开发板上通过 `vpm_run` 验证可加载和运行。

## 1. 已验证基础

开发板环境：

```text
板卡: Orange Pi 4 Pro A733
系统: Orange Pi 1.0.8 Bullseye
架构: aarch64
内核: Linux 6.6.98-sun60iw2
NPU 设备节点: /dev/vipcore
VIPLite driver: 2.0.3.2-AW-2024-08-30
NPU CID: 0x1000003b
```

已部署模型：

```text
/opt/yolov8n/yolov8n_uint8.nb
```

模型信息：

```text
输入: 320 x 320 x 3
输出: 6 x 2100
量化: uint8
NPU 单次推理: 约 2.7 ms
```

当前结论：

```text
模型已经可以在 A733 NPU 上成功加载、prepare 和 run。
后续重点不是继续验证 NPU 是否可用，而是实现摄像头输入、前处理、YOLOv8 后处理和业务逻辑闭环。
```

## 2. 总体目标

构建一个基于 YOLOv8n 的边缘端实时目标检测程序，实现：

```text
1. 从摄像头获取实时图像
2. 将图像转换为 NPU 模型需要的 320x320 RGB 输入
3. 调用 A733 NPU 执行 yolov8n_uint8.nb
4. 解析 6x2100 原始输出，完成置信度过滤和 NMS
5. 输出检测框、类别和置信度
6. 根据检测结果触发业务逻辑，例如 GPIO、MQTT、日志或截图
```

## 3. 实现原则

### 3.1 先闭环，再优化

不要一开始就追求完整零拷贝和 Python 直接操作底层 NPU。优先实现稳定可测的最小闭环：

```text
图片或摄像头帧 -> CPU 前处理 -> NPU 推理 -> CPU 后处理 -> 输出检测结果
```

确认结果正确后，再逐步优化摄像头、内存拷贝和流水线。

### 3.2 核心数据通路优先使用 C/C++

高频数据通路建议用 C/C++ 实现：

```text
V4L2 取帧
YUV/RGB 转换
resize/letterbox
NPU 调用
YOLOv8 后处理
NMS
```

Python 可以用于配置、状态机、业务逻辑和上层调度，但不建议第一版就用 Python 直接通过 `ctypes` 操作底层 VIPLite API。

### 3.3 Python 只做控制路径

如果项目需要 Python，建议 Python 只负责：

```text
读取配置
启动/停止检测
处理检测事件
MQTT/HTTP/日志
GPIO 业务控制
```

不要在 Python 主循环中频繁创建大数组、拷贝视频帧或直接管理 NPU 输入输出 buffer。

## 4. 推荐项目结构

```text
yolo-cap-edge-detection/
├── config/
│   ├── settings.yaml          # 摄像头、FPS、阈值、业务参数
│   └── model_config.yaml      # 模型输入输出、类别、归一化参数
├── models/
│   └── yolov8n_uint8.nb       # A733 NPU 模型
├── native/
│   ├── npu_runner/            # C/C++，封装 VIPLite/NBG 推理
│   │   ├── npu_runner.c
│   │   ├── npu_runner.h
│   │   └── CMakeLists.txt
│   ├── preprocess/            # C/C++，图像前处理
│   │   ├── preprocess.cpp
│   │   └── preprocess.h
│   ├── postprocess/           # C/C++，YOLOv8 decode + NMS
│   │   ├── yolov8_post.cpp
│   │   └── yolov8_post.h
│   └── app/                   # 第一版主程序，可直接全 C/C++
│       ├── main.cpp
│       └── camera_v4l2.cpp
├── src/
│   ├── app.py                 # 可选：Python 上层控制
│   ├── logic.py               # 可选：业务逻辑
│   └── bindings.py            # 可选：调用 native 动态库
└── README.md
```

说明：

```text
第一阶段建议 native/app/main.cpp 直接跑通完整流程。
Python 层可以在第二阶段再接入。
```

## 5. 模块职责

### 5.1 npu_runner

职责：

```text
1. 加载 yolov8n_uint8.nb
2. 初始化 VIPLite/NBG 网络
3. 分配输入输出 buffer
4. 执行 NPU 推理
5. 返回原始输出 tensor
6. 释放 NPU 资源
```

注意：

```text
不要在 Python 中直接向 NPU 寄存器写数据。
应参考 ai-sdk/examples/vpm_run 或 ai-sdk/examples/yolov5 的 C/C++ 调用方式封装。
```

### 5.2 camera_v4l2

第一版职责：

```text
1. 通过 V4L2 或 OpenCV 获取摄像头帧
2. 输出 BGR/RGB/YUV buffer
3. 保证稳定取帧
```

优化版职责：

```text
1. 使用 V4L2 mmap buffer
2. 减少不必要拷贝
3. 与预处理线程使用双缓冲或环形队列
```

### 5.3 preprocess

职责：

```text
1. 将摄像头帧转换为 RGB
2. resize 或 letterbox 到 320x320
3. 按模型要求组织输入 tensor
4. 保持和模型转换时 inputmeta 一致
```

模型转换时的关键预处理参数：

```text
输入尺寸: 320x320
通道顺序: RGB
scale: 1 / 255
mean: 0, 0, 0
```

### 5.4 postprocess

职责：

```text
1. 解析 NPU 输出 6x2100
2. 对每个候选框计算类别和置信度
3. 按阈值过滤候选框
4. 将检测框映射回原图坐标
5. 执行 NMS
6. 输出最终检测结果
```

注意：

```text
YOLOv8 是 anchor-free 输出。
不能直接复用 YOLOv5 的 anchor 解码逻辑。
可以复用 YOLOv5 示例中的 NMS 思路。
```

### 5.5 business logic

职责：

```text
1. 根据检测结果触发 GPIO、MQTT、HTTP 或日志
2. 维护报警状态
3. 控制检测开关和参数热更新
4. 保存必要截图或检测记录
```

该部分可以用 Python 实现，也可以先写入 C++ 主程序中。

## 6. 数据流设计

### 第一阶段：最小可用闭环

```text
图片文件或摄像头单帧
  -> CPU 前处理
  -> NPU 推理
  -> CPU 后处理
  -> 打印检测结果
```

目标：

```text
确认输入、输出、坐标映射和 NMS 正确。
```

### 第二阶段：实时视频流

```text
V4L2 摄像头线程
  -> 环形队列
  -> 推理线程
  -> 后处理
  -> 业务逻辑
```

目标：

```text
实现稳定实时检测。
建议先以 15~30 FPS 为目标。
```

### 第三阶段：性能优化

优化方向：

```text
1. V4L2 mmap buffer
2. 双缓冲或三缓冲
3. C/C++ SIMD/NEON 优化 resize 和 NMS
4. 减少 Python 与 C 之间的数据拷贝
5. 必要时研究 DMA-BUF 或更深的零拷贝方案
```

## 7. 预期性能

当前已测：

```text
NPU 推理: 约 2.7 ms
```

整体帧率主要取决于：

```text
1. 摄像头取帧耗时
2. YUV/RGB 转换耗时
3. resize/letterbox 耗时
4. YOLOv8 后处理和 NMS 耗时
5. Python/C++ 边界拷贝成本
```

预期：

```text
C/C++ 数据通路: 25~30 FPS 有希望
大量 Python/NumPy/OpenCV 拷贝: 可能下降到 10~20 FPS，并且抖动更明显
```

## 8. 第一版开发任务

优先实现以下任务：

```text
1. 创建 native npu_runner，能加载 /opt/yolov8n/yolov8n_uint8.nb
2. 使用已知 input_0.dat 跑一次推理，确认输出 tensor 可读取
3. 实现 YOLOv8 2100x7 后处理
4. 用单张真实图片验证检测框正确
5. 接入摄像头单帧
6. 做实时循环
7. 再考虑 Python 控制层和业务触发
```

第一版验收标准：

```text
1. 程序能在 A733 上启动
2. 能加载 yolov8n_uint8.nb
3. 能从摄像头或图片得到检测结果
4. 检测框坐标基本正确
5. 连续运行 30 分钟无崩溃
```

## 9. 需要避免的设计

暂不建议第一版采用：

```text
1. Python 直接 ctypes 调用底层 VIPLite API
2. Python 直接管理 V4L2 mmap 指针
3. 一开始就做完整零拷贝
4. 直接复用 YOLOv5 anchor 解码
5. 在 main.py 中频繁创建和销毁大 numpy 数组
```

这些优化可以在模型正确、业务闭环稳定后逐步加入。

## 10. 下一步

下一步建议实现：

```text
native/npu_runner + native/postprocess + 单张图片测试程序
```

也就是先做一个正式的 `yolov8n_runner`：

```text
输入: 图片路径或 raw input tensor
输出: 检测框列表
模型: /opt/yolov8n/yolov8n_uint8.nb
```

跑通后，再接入 V4L2 摄像头和业务逻辑。

## 11. 当前开发进展（2026-07-07）

已完成：

```text
1. 创建 cap_detect_app 项目目录和 native/web/config/scripts 基础结构
2. 将 best.onnx 转换并部署为 /opt/yolov8n/yolov8n_uint8.nb
3. 在 A733 上接入 VIPLite/NBG，cap_detect 可加载 .nb 并读取输出 tensor
4. 实现 YOLOv8 输出解码、置信度过滤和 NMS 的基础版本
5. 接入 IMX219 摄像头单帧采集，完成 YUV -> BGR/RGB -> 320x320 CHW 输入
6. 解决当前板端 IMX219 驱动默认 4-lane 与硬件 PORT2 2-lane 不匹配问题，脚本可加载临时 2-lane 模块
7. cap_detect 支持 --camera /dev/video0 --snapshot latest.jpg --json
8. Web 页面可通过 http://192.168.0.102:8080 访问，显示摄像头快照、NPU 原始输出、检测框和实时轮询状态
9. Web 后端提供 /api/status、/api/run-test、/api/start-live、/api/stop-live、/api/live、/api/events
10. 增加检测事件 JSONL 记录入口，出现检测框时写入 data/events.jsonl
```

当前验证结果：

```text
1. /dev/video0、/dev/media0、/dev/v4l-subdev0 均存在
2. /api/run-test 使用摄像头输入并成功保存 web/static/latest.jpg
3. 单次摄像头采集 + NPU 推理 + JSON 返回耗时约 500~700 ms
4. latest.jpg 可被浏览器访问
5. /api/start-live 可连续轮询，/api/stop-live 可停止
6. 当前检测结果为空，raw_stats.max_class_score = 0.0000
```

仍待完成：

```text
1. 确认训练类别名称，将 class_0/class_1 替换为真实业务标签
2. 排查 max_class_score 始终为 0 的原因：重点检查量化输入、scale/mean、RGB/BGR、CHW/HWC、输出 tensor 排布和模型转换参数
3. 用包含目标且与训练集一致的图片/现场画面验证检测框坐标是否正确
4. 将当前按请求启动 cap_detect 的实时轮询改为常驻 native 检测进程，避免每帧重复加载模型和初始化摄像头
5. 做 30 分钟稳定性测试，记录帧率、内存、NPU/摄像头错误
6. 根据业务需求接入 GPIO、MQTT、HTTP 回调或本地告警状态机
7. 决定是否将 IMX219 2-lane 驱动修正做成持久化部署，当前只用临时 /tmp/imx219_2lane.ko
```

下一步优先级：

```text
优先处理模型输出分数为 0 的问题。只有先确认模型能对真实目标输出非零类别分数，后续 GPIO/MQTT/报警逻辑才有可靠触发依据。
```
