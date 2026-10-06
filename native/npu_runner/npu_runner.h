#pragma once

#include <string>
#include <vector>

namespace cap {

class NpuRunner {
public:
    NpuRunner();
    ~NpuRunner();

    NpuRunner(const NpuRunner&) = delete;
    NpuRunner& operator=(const NpuRunner&) = delete;

    void load(const std::string& model_path);
    std::vector<float> run_tensor_file(const std::string& input_path);
    std::vector<float> run_tensor_data(const std::vector<unsigned char>& input);
    std::vector<float> run_dummy();

private:
    std::string model_path_;
    void* context_ = nullptr;
};

}  // namespace cap
