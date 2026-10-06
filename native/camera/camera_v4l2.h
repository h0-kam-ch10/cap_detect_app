#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <linux/videodev2.h>

namespace cap {

class V4L2Camera {
public:
    V4L2Camera(const std::string& device, int width, int height);
    ~V4L2Camera();

    bool init();
    bool start();
    bool stop();
    bool get_frame(cv::Mat& frame);

private:
    struct Buffer {
        void* start[3] = {nullptr, nullptr, nullptr};
        size_t length[3] = {0, 0, 0};
    };

    bool init_device();
    bool set_format();
    bool request_buffers();
    bool queue_all_buffers();
    void release_buffers();
    void yuv420m_to_bgr(unsigned char* y_plane, unsigned char* u_plane, unsigned char* v_plane, cv::Mat& bgr_frame);
    void packed_bgr_to_mat(unsigned char* data, cv::Mat& bgr_frame);

    static constexpr int BUFFER_COUNT = 4;

    std::string device_;
    int width_;
    int height_;
    int fd_ = -1;
    bool streaming_ = false;
    bool initialized_ = false;
    Buffer buffers_[BUFFER_COUNT];
    unsigned int nplanes_ = 3;
    unsigned int pixel_format_ = V4L2_PIX_FMT_BGR24;
    unsigned int bytes_per_line_[3] = {0, 0, 0};
};

std::vector<unsigned char> bgr_to_chw_rgb_320(const cv::Mat& bgr, cv::Mat* preview_rgb = nullptr);

}  // namespace cap
