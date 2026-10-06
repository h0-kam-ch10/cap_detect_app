#include "yolov8_post.h"

#include <algorithm>
#include <stdexcept>

namespace cap {

namespace {

float iou(const Detection& a, const Detection& b) {
    const float x1 = std::max(a.x1, b.x1);
    const float y1 = std::max(a.y1, b.y1);
    const float x2 = std::min(a.x2, b.x2);
    const float y2 = std::min(a.y2, b.y2);
    const float w = std::max(0.0f, x2 - x1);
    const float h = std::max(0.0f, y2 - y1);
    const float inter = w * h;
    const float area_a = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float area_b = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    const float denom = area_a + area_b - inter;
    return denom > 0.0f ? inter / denom : 0.0f;
}

}  // namespace

std::vector<Detection> decode_yolov8(
    const std::vector<float>& output,
    const YoloV8PostConfig& config) {
    const int expected = config.output_channels * config.candidates;
    if (static_cast<int>(output.size()) != expected) {
        throw std::runtime_error("unexpected YOLOv8 output size");
    }

    std::vector<Detection> candidates;
    candidates.reserve(config.candidates);

    for (int i = 0; i < config.candidates; ++i) {
        const float cx = output[0 * config.candidates + i];
        const float cy = output[1 * config.candidates + i];
        const float w = output[2 * config.candidates + i];
        const float h = output[3 * config.candidates + i];

        int best_class = -1;
        float best_score = 0.0f;
        for (int c = 0; c < config.class_count; ++c) {
            const float raw_score = output[(4 + c) * config.candidates + i];
            const float score = raw_score / std::max(1.0f, config.class_score_scale);
            if (score > best_score) {
                best_score = score;
                best_class = c;
            }
        }

        if (best_score < config.confidence_threshold) {
            continue;
        }

        candidates.push_back({
            best_class,
            best_score,
            cx - w * 0.5f,
            cy - h * 0.5f,
            cx + w * 0.5f,
            cy + h * 0.5f,
        });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Detection& a, const Detection& b) {
        return a.score > b.score;
    });

    std::vector<Detection> result;
    for (const auto& det : candidates) {
        bool suppressed = false;
        for (const auto& kept : result) {
            if (det.class_id == kept.class_id && iou(det, kept) > config.nms_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            result.push_back(det);
        }
    }

    return result;
}

}  // namespace cap
