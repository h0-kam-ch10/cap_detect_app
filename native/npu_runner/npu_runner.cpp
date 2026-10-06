#include "npu_runner.h"

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifdef CAP_USE_VIPLITE
extern "C" {
#include "awnn_lib.h"
}
#endif

namespace cap {

namespace {

constexpr std::size_t kInputBytes = 320 * 320 * 3;
constexpr std::size_t kOutputElements = 6 * 2100;
constexpr std::size_t kCandidates = 2100;
constexpr std::size_t kBboxElements = 4 * kCandidates;
constexpr std::size_t kClassElements = 2 * kCandidates;

std::vector<std::uint8_t> read_input_tensor(const std::string& input_path) {
    if (!std::filesystem::exists(input_path)) {
        throw std::runtime_error("input tensor not found: " + input_path);
    }

    std::ifstream input(input_path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open input tensor: " + input_path);
    }

    const auto size = input.tellg();
    if (size != static_cast<std::streamoff>(kInputBytes)) {
        throw std::runtime_error("expected CHW RGB uint8 tensor size 307200 bytes");
    }

    std::vector<std::uint8_t> data(kInputBytes);
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input) {
        throw std::runtime_error("failed to read input tensor: " + input_path);
    }
    return data;
}

#ifdef CAP_USE_VIPLITE
void ensure_viplite_init() {
    static bool initialized = false;
    if (!initialized) {
        awnn_init();
        initialized = true;
    }
}
#endif

}  // namespace

NpuRunner::NpuRunner() = default;

NpuRunner::~NpuRunner() {
#ifdef CAP_USE_VIPLITE
    if (context_ != nullptr) {
        awnn_destroy(reinterpret_cast<Awnn_Context_t*>(context_));
        context_ = nullptr;
    }
#endif
}

void NpuRunner::load(const std::string& model_path) {
    if (!std::filesystem::exists(model_path)) {
        throw std::runtime_error("model not found: " + model_path);
    }

#ifdef CAP_USE_VIPLITE
    ensure_viplite_init();
    if (context_ != nullptr) {
        awnn_destroy(reinterpret_cast<Awnn_Context_t*>(context_));
        context_ = nullptr;
    }
    Awnn_Context_t* ctx = awnn_create(model_path.c_str());
    if (ctx == nullptr) {
        throw std::runtime_error("failed to create VIPLite network from: " + model_path);
    }
    context_ = ctx;
#endif

    model_path_ = model_path;
}

std::vector<float> NpuRunner::run_tensor_file(const std::string& input_path) {
    auto input = read_input_tensor(input_path);
    return run_tensor_data(input);
}

std::vector<float> NpuRunner::run_tensor_data(const std::vector<unsigned char>& input) {
    if (model_path_.empty()) {
        throw std::runtime_error("model is not loaded");
    }
    if (input.size() != kInputBytes) {
        throw std::runtime_error("expected CHW RGB uint8 tensor size 307200 bytes");
    }

#ifdef CAP_USE_VIPLITE
    if (context_ == nullptr) {
        throw std::runtime_error("VIPLite network is not loaded");
    }
    void* inputs[1] = {const_cast<unsigned char*>(input.data())};
    auto* ctx = reinterpret_cast<Awnn_Context_t*>(context_);
    awnn_set_input_buffers(ctx, inputs);
    awnn_run(ctx);
    float** outputs = awnn_get_output_buffers(ctx);
    if (outputs == nullptr || outputs[0] == nullptr) {
        throw std::runtime_error("VIPLite output buffer is null");
    }
    const int output_count = awnn_get_output_count(ctx);
    if (output_count == 1) {
        const unsigned int elements = awnn_get_output_elements(ctx, 0);
        if (elements != kOutputElements) {
            throw std::runtime_error("unexpected single-output YOLOv8 element count");
        }
        return std::vector<float>(outputs[0], outputs[0] + kOutputElements);
    }
    if (output_count == 2) {
        if (outputs[1] == nullptr) {
            throw std::runtime_error("VIPLite second output buffer is null");
        }
        const unsigned int elements0 = awnn_get_output_elements(ctx, 0);
        const unsigned int elements1 = awnn_get_output_elements(ctx, 1);
        float* bbox_output = nullptr;
        float* class_output = nullptr;
        if (elements0 == kBboxElements && elements1 == kClassElements) {
            bbox_output = outputs[0];
            class_output = outputs[1];
        } else if (elements0 == kClassElements && elements1 == kBboxElements) {
            bbox_output = outputs[1];
            class_output = outputs[0];
        } else {
            throw std::runtime_error("unexpected dual-output YOLOv8 element count");
        }
        std::vector<float> merged(kOutputElements);
        std::copy(bbox_output, bbox_output + kBboxElements, merged.begin());
        std::copy(class_output, class_output + kClassElements, merged.begin() + kBboxElements);
        return merged;
    }
    throw std::runtime_error("unexpected YOLOv8 output tensor count");
#else
    return run_dummy();
#endif
}

std::vector<float> NpuRunner::run_dummy() {
    return std::vector<float>(kOutputElements, 0.0f);
}

}  // namespace cap
