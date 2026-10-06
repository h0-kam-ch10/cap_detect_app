#pragma once

#include <vector>

namespace cap {

struct Detection {
    int class_id = -1;
    float score = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
};

struct YoloV8PostConfig {
    int output_channels = 6;
    int candidates = 2100;
    int class_count = 2;
    float confidence_threshold = 0.35f;
    float nms_threshold = 0.45f;
    float class_score_scale = 1.0f;
};

std::vector<Detection> decode_yolov8(
    const std::vector<float>& output,
    const YoloV8PostConfig& config);

}  // namespace cap
