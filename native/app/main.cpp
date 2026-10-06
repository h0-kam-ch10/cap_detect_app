#include "npu_runner/npu_runner.h"
#include "postprocess/yolov8_post.h"
#ifdef CAP_USE_OPENCV
#include "camera/camera_v4l2.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop(false);

struct Args {
    std::string model_path = "/opt/yolov8n/yolov8n_uint8.nb";
    std::string input_tensor_path;
    std::string camera_device;
    std::string snapshot_path;
    std::string result_path;
    bool json = false;
    bool loop = false;
    int interval_ms = 0;
    float class_score_scale = 1.0f;
    double preview_gain = 1.0;
    double preview_gamma = 1.0;
    double preview_clahe = 0.0;
    double preview_awb_strength = 0.6;
    double preview_awb_red_bias = 0.66;
    double preview_awb_blue_bias = 0.70;
    double preview_saturation = 1.55;
    double preview_hue_shift = 0.0;
    double input_awb_strength = 0.8;
    double input_awb_red_bias = 0.74;
    double input_awb_blue_bias = 0.78;
    double input_saturation = 1.25;
    double input_hue_shift = 0.0;
    double input_gain = 1.0;
    double input_gamma = 0.95;
    double input_clahe = 0.0;
    double input_sharpen = 0.6;
    int jpeg_quality = 80;
};

// 功能：接收 SIGINT/SIGTERM 后设置全局退出标志；由 std::signal 注册调用。
void handle_signal(int) {
    g_stop.store(true);
}

// 功能：解析命令行选项并填充 Args；使用 std::atoi、std::atof、std::exit，非法参数抛出异常。
Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--model" && i + 1 < argc) {
            args.model_path = argv[++i];
        } else if (key == "--input" && i + 1 < argc) {
            args.input_tensor_path = argv[++i];
        } else if (key == "--camera" && i + 1 < argc) {
            args.camera_device = argv[++i];
        } else if (key == "--snapshot" && i + 1 < argc) {
            args.snapshot_path = argv[++i];
        } else if (key == "--result" && i + 1 < argc) {
            args.result_path = argv[++i];
        } else if (key == "--interval-ms" && i + 1 < argc) {
            args.interval_ms = std::atoi(argv[++i]);
        } else if (key == "--class-scale" && i + 1 < argc) {
            args.class_score_scale = std::atof(argv[++i]);
        } else if (key == "--preview-gain" && i + 1 < argc) {
            args.preview_gain = std::atof(argv[++i]);
        } else if (key == "--preview-gamma" && i + 1 < argc) {
            args.preview_gamma = std::atof(argv[++i]);
        } else if (key == "--preview-clahe" && i + 1 < argc) {
            args.preview_clahe = std::atof(argv[++i]);
        } else if (key == "--preview-awb-strength" && i + 1 < argc) {
            args.preview_awb_strength = std::atof(argv[++i]);
        } else if (key == "--preview-awb-red-bias" && i + 1 < argc) {
            args.preview_awb_red_bias = std::atof(argv[++i]);
        } else if (key == "--preview-awb-blue-bias" && i + 1 < argc) {
            args.preview_awb_blue_bias = std::atof(argv[++i]);
        } else if (key == "--preview-saturation" && i + 1 < argc) {
            args.preview_saturation = std::atof(argv[++i]);
        } else if (key == "--preview-hue-shift" && i + 1 < argc) {
            args.preview_hue_shift = std::atof(argv[++i]);
        } else if (key == "--input-awb-strength" && i + 1 < argc) {
            args.input_awb_strength = std::atof(argv[++i]);
        } else if (key == "--input-awb-red-bias" && i + 1 < argc) {
            args.input_awb_red_bias = std::atof(argv[++i]);
        } else if (key == "--input-awb-blue-bias" && i + 1 < argc) {
            args.input_awb_blue_bias = std::atof(argv[++i]);
        } else if (key == "--input-saturation" && i + 1 < argc) {
            args.input_saturation = std::atof(argv[++i]);
        } else if (key == "--input-hue-shift" && i + 1 < argc) {
            args.input_hue_shift = std::atof(argv[++i]);
        } else if (key == "--input-gain" && i + 1 < argc) {
            args.input_gain = std::atof(argv[++i]);
        } else if (key == "--input-gamma" && i + 1 < argc) {
            args.input_gamma = std::atof(argv[++i]);
        } else if (key == "--input-clahe" && i + 1 < argc) {
            args.input_clahe = std::atof(argv[++i]);
        } else if (key == "--input-sharpen" && i + 1 < argc) {
            args.input_sharpen = std::atof(argv[++i]);
        } else if (key == "--jpeg-quality" && i + 1 < argc) {
            args.jpeg_quality = std::atoi(argv[++i]);
        } else if (key == "--json") {
            args.json = true;
        } else if (key == "--loop") {
            args.loop = true;
        } else if (key == "--help") {
            std::cout << "Usage: cap_detect [--model path] [--input chw_rgb_320.dat] [--camera /dev/video0] [--snapshot latest.jpg] [--result latest.json] [--loop] [--interval-ms n] [--class-scale n] [--preview-gain n] [--preview-gamma n] [--preview-clahe n] [--preview-awb-strength n] [--preview-awb-red-bias n] [--preview-awb-blue-bias n] [--preview-saturation n] [--preview-hue-shift deg] [--input-awb-strength n] [--input-awb-red-bias n] [--input-awb-blue-bias n] [--input-saturation n] [--input-hue-shift deg] [--input-gain n] [--input-gamma n] [--input-clahe n] [--input-sharpen n] [--jpeg-quality n] [--json]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + key);
        }
    }
    return args;
}

// 功能：转义字符串中的 JSON 特殊字符；由 make_json 调用。
std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

// 功能：生成带毫秒的本地时间字符串；使用 system_clock、localtime_r、std::put_time；由 make_json 调用。
std::string now_ms_string() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." << std::setw(3) << std::setfill('0') << ms.count();
    return out.str();
}

struct ChannelStats {
    float min_value = 0.0f;
    float max_value = 0.0f;
};

struct RawStats {
    float min_value = 0.0f;
    float max_value = 0.0f;
    float max_class_score = 0.0f;
    int max_class_id = -1;
    int max_candidate = -1;
    float alt_layout_max_class_score = 0.0f;
    int alt_layout_max_class_id = -1;
    int alt_layout_max_candidate = -1;
    std::vector<ChannelStats> channels;
};

// 功能：统计原始模型输出的范围、各通道范围和最高类别得分；使用 std::min/std::max/isfinite；由 make_json 调用。
RawStats compute_stats(const std::vector<float>& raw_output, float class_score_scale) {
    RawStats stats;
    if (raw_output.empty()) return stats;
    stats.min_value = std::numeric_limits<float>::infinity();
    stats.max_value = -std::numeric_limits<float>::infinity();
    stats.max_class_score = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < raw_output.size(); ++i) {
        stats.min_value = std::min(stats.min_value, raw_output[i]);
        stats.max_value = std::max(stats.max_value, raw_output[i]);
    }
    constexpr int candidates_count = 2100;
    constexpr int class_count = 2;
    constexpr int output_channels = 6;
    stats.channels.assign(output_channels, {});
    if (raw_output.size() >= static_cast<size_t>(output_channels * candidates_count)) {
        for (int ch = 0; ch < output_channels; ++ch) {
            stats.channels[ch].min_value = std::numeric_limits<float>::infinity();
            stats.channels[ch].max_value = -std::numeric_limits<float>::infinity();
            for (int i = 0; i < candidates_count; ++i) {
                const float value = raw_output[ch * candidates_count + i];
                stats.channels[ch].min_value = std::min(stats.channels[ch].min_value, value);
                stats.channels[ch].max_value = std::max(stats.channels[ch].max_value, value);
            }
        }
        for (int i = 0; i < candidates_count; ++i) {
            for (int c = 0; c < class_count; ++c) {
                const float score = raw_output[(4 + c) * candidates_count + i] / std::max(1.0f, class_score_scale);
                if (score > stats.max_class_score) {
                    stats.max_class_score = score;
                    stats.max_class_id = c;
                    stats.max_candidate = i;
                }
                const float alt_score = raw_output[i * output_channels + 4 + c];
                if (alt_score > stats.alt_layout_max_class_score) {
                    stats.alt_layout_max_class_score = alt_score;
                    stats.alt_layout_max_class_id = c;
                    stats.alt_layout_max_candidate = i;
                }
            }
        }
    }
    if (!std::isfinite(stats.max_class_score)) stats.max_class_score = 0.0f;
    return stats;
}

// 功能：将推理结果、耗时和原始输出统计序列化为 JSON；调用 compute_stats、json_escape、now_ms_string。
std::string make_json(const Args& args,
                      const std::string& source,
                      const std::vector<float>& raw_output,
                      const std::vector<cap::Detection>& detections,
                      int frame_id,
                      int elapsed_ms) {
    const RawStats stats = compute_stats(raw_output, args.class_score_scale);
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"ok\": true,\n";
    out << "  \"timestamp\": \"" << json_escape(now_ms_string()) << "\",\n";
    out << "  \"frame_id\": " << frame_id << ",\n";
    out << "  \"elapsed_ms\": " << elapsed_ms << ",\n";
    out << "  \"model\": \"" << json_escape(args.model_path) << "\",\n";
    out << "  \"input\": \"" << json_escape(source) << "\",\n";
    out << "  \"snapshot\": \"" << json_escape(args.snapshot_path) << "\",\n";
    out << "  \"raw_stats\": {\"elements\":" << raw_output.size()
        << ",\"min\":" << stats.min_value
        << ",\"max\":" << stats.max_value
        << ",\"max_class_score\":" << stats.max_class_score
        << ",\"max_class_id\":" << stats.max_class_id
        << ",\"max_candidate\":" << stats.max_candidate
        << ",\"alt_layout_max_class_score\":" << stats.alt_layout_max_class_score
        << ",\"alt_layout_max_class_id\":" << stats.alt_layout_max_class_id
        << ",\"alt_layout_max_candidate\":" << stats.alt_layout_max_candidate
        << ",\"channels\":[";
    for (size_t i = 0; i < stats.channels.size(); ++i) {
        if (i != 0) out << ",";
        out << "{\"min\":" << stats.channels[i].min_value
            << ",\"max\":" << stats.channels[i].max_value << "}";
    }
    out << "]},\n";
    out << "  \"detections\": [";
    for (size_t i = 0; i < detections.size(); ++i) {
        const auto& det = detections[i];
        if (i != 0) out << ",";
        out << "{\"class_id\":" << det.class_id
            << ",\"score\":" << det.score
            << ",\"x1\":" << det.x1
            << ",\"y1\":" << det.y1
            << ",\"x2\":" << det.x2
            << ",\"y2\":" << det.y2 << "}";
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}

// 功能：先写入临时文件再重命名，原子更新结果文件；使用 std::ofstream、std::rename；由 run_once/run_loop 调用。
void write_text_atomic(const std::string& path, const std::string& text) {
    if (path.empty()) return;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::out | std::ios::trunc);
        if (!f) throw std::runtime_error("failed to open result tmp file: " + tmp);
        f << text;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        throw std::runtime_error("failed to rename result file: " + path);
    }
}

#ifdef CAP_USE_OPENCV
// 功能：按灰度世界假设对白平衡进行校正；调用 cv::mean、cv::split、Mat::convertTo、cv::merge。
cv::Mat apply_awb_rgb(const cv::Mat& rgb,
                      double strength,
                      double red_bias,
                      double blue_bias) {
    if (rgb.empty() || strength <= 0.0) {
        return rgb.clone();
    }

    const cv::Scalar mean_rgb = cv::mean(rgb);
    const double red_mean = mean_rgb[0];
    const double green_mean = mean_rgb[1];
    const double blue_mean = mean_rgb[2];
    const double min_mean = std::min({red_mean, green_mean, blue_mean});
    if (min_mean <= 1e-6) {
        return rgb.clone();
    }

    const double gray_mean = (red_mean + green_mean + blue_mean) / 3.0;
    std::array<double, 3> gains = {
        gray_mean / red_mean,
        gray_mean / green_mean,
        gray_mean / blue_mean,
    };
    gains[0] *= std::max(0.1, red_bias);
    gains[2] *= std::max(0.1, blue_bias);

    for (double& gain : gains) {
        gain = 1.0 + (gain - 1.0) * strength;
        gain = std::clamp(gain, 0.7, 1.35);
    }

    std::vector<cv::Mat> channels;
    cv::split(rgb, channels);
    for (size_t i = 0; i < channels.size() && i < gains.size(); ++i) {
        channels[i].convertTo(channels[i], -1, gains[i], 0.0);
    }

    cv::Mat balanced;
    cv::merge(channels, balanced);
    return balanced;
}

// 功能：调整 RGB 图像的饱和度和色相；调用 cv::cvtColor、Mat::ptr、cv::saturate_cast。
cv::Mat apply_hsv_rgb(const cv::Mat& rgb,
                     double saturation_scale,
                     double hue_shift_deg) {
    if (rgb.empty() || (std::abs(saturation_scale - 1.0) <= 0.001 && std::abs(hue_shift_deg) <= 0.001)) {
        return rgb.clone();
    }

    cv::Mat hsv;
    cv::cvtColor(rgb, hsv, cv::COLOR_RGB2HSV);
    for (int y = 0; y < hsv.rows; ++y) {
        cv::Vec3b* row = hsv.ptr<cv::Vec3b>(y);
        for (int x = 0; x < hsv.cols; ++x) {
            int hue = row[x][0] + static_cast<int>(std::round(hue_shift_deg / 2.0));
            hue %= 180;
            if (hue < 0) hue += 180;
            row[x][0] = static_cast<uchar>(hue);
            row[x][1] = cv::saturate_cast<uchar>(row[x][1] * std::clamp(saturation_scale, 0.0, 3.0));
        }
    }

    cv::Mat adjusted;
    cv::cvtColor(hsv, adjusted, cv::COLOR_HSV2RGB);
    return adjusted;
}

// 功能：组合白平衡、HSV、增益、Gamma、CLAHE 和锐化；调用 apply_awb_rgb、apply_hsv_rgb 及 OpenCV 图像处理函数。
cv::Mat enhance_rgb(const cv::Mat& rgb,
                    double awb_strength,
                    double awb_red_bias,
                    double awb_blue_bias,
                    double saturation_scale,
                    double hue_shift_deg,
                    double gain,
                    double gamma,
                    double clahe_clip,
                    double sharpen_amount) {
    cv::Mat enhanced = apply_awb_rgb(rgb, awb_strength, awb_red_bias, awb_blue_bias);
    enhanced = apply_hsv_rgb(enhanced, saturation_scale, hue_shift_deg);
    if (gain > 0.0 && std::abs(gain - 1.0) > 0.001) {
        enhanced.convertTo(enhanced, -1, gain, 0.0);
    }
    if (gamma > 0.0 && std::abs(gamma - 1.0) > 0.001) {
        cv::Mat lut(1, 256, CV_8UC1);
        uchar* table = lut.ptr<uchar>(0);
        for (int i = 0; i < 256; ++i) {
            table[i] = cv::saturate_cast<uchar>(std::pow(i / 255.0, gamma) * 255.0);
        }
        cv::LUT(enhanced, lut, enhanced);
    }
    if (clahe_clip > 0.0) {
        cv::Mat lab;
        cv::cvtColor(enhanced, lab, cv::COLOR_RGB2Lab);
        std::vector<cv::Mat> channels;
        cv::split(lab, channels);
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clahe_clip, cv::Size(8, 8));
        clahe->apply(channels[0], channels[0]);
        cv::merge(channels, lab);
        cv::cvtColor(lab, enhanced, cv::COLOR_Lab2RGB);
    }
    if (sharpen_amount > 0.0) {
        cv::Mat blurred;
        cv::GaussianBlur(enhanced, blurred, cv::Size(0, 0), 1.0);
        cv::addWeighted(enhanced, 1.0 + sharpen_amount, blurred, -sharpen_amount, 0.0, enhanced);
    }
    return enhanced;
}

// 功能：将 320x320 的 RGB HWC 图像转换为模型所需的 CHW uint8 张量；由 run_rgb_frame 调用。
std::vector<unsigned char> rgb_to_chw_tensor(const cv::Mat& rgb) {
    if (rgb.empty() || rgb.cols != 320 || rgb.rows != 320 || rgb.type() != CV_8UC3) {
        throw std::runtime_error("expected 320x320 RGB preview for tensor conversion");
    }
    std::vector<unsigned char> tensor(320 * 320 * 3);
    const int plane = 320 * 320;
    for (int y = 0; y < 320; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < 320; ++x) {
            const int idx = y * 320 + x;
            tensor[idx] = row[x][0];
            tensor[plane + idx] = row[x][1];
            tensor[2 * plane + idx] = row[x][2];
        }
    }
    return tensor;
}

// 功能：预处理相机帧、可选保存预览 JPEG，并执行 NPU 推理；调用 bgr_to_chw_rgb_320、enhance_rgb、rgb_to_chw_tensor、imwrite、run_tensor_data。
std::vector<float> run_rgb_frame(cap::NpuRunner& runner,
                                 const cv::Mat& frame,
                                 const Args& args) {
    const std::string& snapshot_path = args.snapshot_path;
    if (frame.empty()) {
        throw std::runtime_error("empty camera frame");
    }
    cv::Mat preview_rgb;
    cap::bgr_to_chw_rgb_320(frame, &preview_rgb);
    cv::Mat model_rgb = enhance_rgb(preview_rgb,
                                    args.input_awb_strength,
                                    args.input_awb_red_bias,
                                    args.input_awb_blue_bias,
                                    args.input_saturation,
                                    args.input_hue_shift,
                                    args.input_gain,
                                    args.input_gamma,
                                    args.input_clahe,
                                    args.input_sharpen);
    auto tensor = rgb_to_chw_tensor(model_rgb);
    if (!snapshot_path.empty()) {
        cv::Mat preview_to_save = enhance_rgb(preview_rgb,
                                             args.preview_awb_strength,
                                             args.preview_awb_red_bias,
                                             args.preview_awb_blue_bias,
                                             args.preview_saturation,
                                             args.preview_hue_shift,
                                             args.preview_gain,
                                             args.preview_gamma,
                                             args.preview_clahe,
                                             args.input_sharpen * 0.5);
        cv::Mat preview_bgr;
        cv::cvtColor(preview_to_save, preview_bgr, cv::COLOR_RGB2BGR);
        const std::string tmp_snapshot = snapshot_path + ".tmp.jpg";
        std::vector<int> jpeg_params = {cv::IMWRITE_JPEG_QUALITY, std::clamp(args.jpeg_quality, 35, 95)};
        if (!cv::imwrite(tmp_snapshot, preview_bgr, jpeg_params)) {
            throw std::runtime_error("failed to save snapshot tmp file: " + tmp_snapshot);
        }
        if (std::rename(tmp_snapshot.c_str(), snapshot_path.c_str()) != 0) {
            throw std::runtime_error("failed to rename snapshot file: " + snapshot_path);
        }
    }
    return runner.run_tensor_data(tensor);
}

// 功能：从 V4L2 相机取一帧并执行推理；调用 V4L2Camera::get_frame 和 run_rgb_frame。
std::vector<float> run_camera_frame(cap::NpuRunner& runner,
                                    cap::V4L2Camera& camera,
                                    const Args& args) {
    cv::Mat frame;
    if (!camera.get_frame(frame)) {
        throw std::runtime_error("failed to capture camera frame");
    }
    return run_rgb_frame(runner, frame, args);
}

struct SharedFrame {
    std::mutex mutex;
    std::condition_variable cv;
    cv::Mat frame;
    int64_t sequence = 0;
    bool stopped = false;
    bool failed = false;
    std::string error;
};

// 功能：记录采集线程错误并唤醒等待者；使用 std::lock_guard 和 condition_variable::notify_all；由 capture_loop 调用。
void set_capture_error(SharedFrame& shared, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.failed = true;
        shared.stopped = true;
        shared.error = error;
    }
    shared.cv.notify_all();
}

// 功能：在后台持续采集最新相机帧；调用 V4L2Camera::init/start/get_frame、Mat::clone 和 set_capture_error。
void capture_loop(const Args& args, SharedFrame& shared) {
    try {
        cap::V4L2Camera camera(args.camera_device, 640, 480);
        if (!camera.init() || !camera.start()) {
            throw std::runtime_error("failed to initialize camera: " + args.camera_device);
        }

        int consecutive_failures = 0;
        while (!g_stop.load()) {
            cv::Mat frame;
            if (camera.get_frame(frame) && !frame.empty()) {
                consecutive_failures = 0;
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.frame = frame.clone();
                    ++shared.sequence;
                }
                shared.cv.notify_all();
                continue;
            }

            ++consecutive_failures;
            if (consecutive_failures >= 5) {
                throw std::runtime_error("camera capture stalled");
            }
        }

        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.stopped = true;
        }
        shared.cv.notify_all();
    } catch (const std::exception& e) {
        set_capture_error(shared, e.what());
    }
}

// 功能：等待比指定序号更新的相机帧；调用 condition_variable::wait_for 和 Mat::clone；由 run_loop 调用。
bool wait_for_frame(SharedFrame& shared,
                    int64_t last_sequence,
                    cv::Mat& frame,
                    int64_t& sequence,
                    std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(shared.mutex);
    const bool ready = shared.cv.wait_for(lock, timeout, [&shared, last_sequence] {
        return shared.sequence > last_sequence || shared.failed || g_stop.load();
    });
    if (shared.failed) {
        throw std::runtime_error(shared.error.empty() ? "camera capture failed" : shared.error);
    }
    if (!ready || shared.sequence <= last_sequence || shared.frame.empty()) {
        return false;
    }
    frame = shared.frame.clone();
    sequence = shared.sequence;
    return true;
}
#endif

// 功能：完成一次输入读取、推理、YOLO 解码和结果输出；调用相机/NPU 接口、decode_yolov8、make_json、write_text_atomic。
int run_once(const Args& args, cap::NpuRunner& runner) {
    std::vector<float> raw_output;
    std::string source = args.input_tensor_path;
    const auto started = std::chrono::steady_clock::now();
#ifdef CAP_USE_OPENCV
    if (!args.camera_device.empty()) {
        cap::V4L2Camera camera(args.camera_device, 640, 480);
        if (!camera.init() || !camera.start()) {
            throw std::runtime_error("failed to initialize camera: " + args.camera_device);
        }
        cv::Mat warmup;
        for (int i = 0; i < 4; ++i) camera.get_frame(warmup);
        raw_output = run_camera_frame(runner, camera, args);
        source = args.camera_device;
    } else
#endif
    if (!args.input_tensor_path.empty()) {
        raw_output = runner.run_tensor_file(args.input_tensor_path);
    } else {
        raw_output = runner.run_dummy();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    cap::YoloV8PostConfig post_cfg;
    post_cfg.class_score_scale = args.class_score_scale;
    const auto detections = cap::decode_yolov8(raw_output, post_cfg);
    const std::string payload = make_json(args, source, raw_output, detections, 1, static_cast<int>(elapsed));
    write_text_atomic(args.result_path, payload);
    if (args.json) std::cout << payload;
    return 0;
}

// 功能：启动采集线程并持续处理最新帧；调用 capture_loop、wait_for_frame、run_rgb_frame、decode_yolov8、make_json、write_text_atomic。
int run_loop(const Args& args, cap::NpuRunner& runner) {
#ifndef CAP_USE_OPENCV
    throw std::runtime_error("loop mode requires OpenCV camera support");
#else
    if (args.camera_device.empty()) {
        throw std::runtime_error("loop mode requires --camera");
    }
    SharedFrame shared;
    std::thread capture_thread(capture_loop, std::cref(args), std::ref(shared));
    auto stop_capture = [&capture_thread, &shared] {
        g_stop.store(true);
        shared.cv.notify_all();
        if (capture_thread.joinable()) capture_thread.join();
    };

    cv::Mat frame;
    int64_t last_sequence = 0;
    try {
        if (!wait_for_frame(shared, last_sequence, frame, last_sequence, std::chrono::seconds(10))) {
            throw std::runtime_error("timed out waiting for first camera frame");
        }
    } catch (...) {
        stop_capture();
        throw;
    }

    int frame_id = 0;
    try {
        while (!g_stop.load()) {
            const auto started = std::chrono::steady_clock::now();
            if (!wait_for_frame(shared, last_sequence, frame, last_sequence, std::chrono::seconds(3))) {
                throw std::runtime_error("timed out waiting for camera frame");
            }
            auto raw_output = run_rgb_frame(runner, frame, args);
            cap::YoloV8PostConfig post_cfg;
            post_cfg.class_score_scale = args.class_score_scale;
            const auto detections = cap::decode_yolov8(raw_output, post_cfg);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            ++frame_id;
            const std::string payload = make_json(args, args.camera_device, raw_output, detections, frame_id, static_cast<int>(elapsed));
            write_text_atomic(args.result_path, payload);
            if (args.json) std::cout << payload << std::flush;
            const int sleep_ms = args.interval_ms - static_cast<int>(elapsed);
            if (sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        }
    } catch (...) {
        stop_capture();
        throw;
    }
    stop_capture();
    return 0;
#endif
}

}  // namespace

// 功能：程序入口，注册信号、读取参数、加载模型，并选择单次或循环检测。
int main(int argc, char** argv) {
    try {
        // 注册 Ctrl+C 和终止信号的处理函数，触发后由 handle_signal 设置 g_stop。
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        // 解析 --model、--camera、--loop 等命令行参数。
        const Args args = parse_args(argc, argv);
        // 创建 NPU 推理器并加载指定模型文件。
        cap::NpuRunner runner;
        runner.load(args.model_path);
        // 根据 --loop 选择单帧处理 run_once 或连续处理 run_loop。
        return args.loop ? run_loop(args, runner) : run_once(args, runner);
    } catch (const std::exception& e) {
        std::cerr << "cap_detect failed: " << e.what() << "\n";
        return 1;
    }
}
