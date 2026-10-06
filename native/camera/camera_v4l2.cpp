#include "camera_v4l2.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define V4L2_MODE_VIDEO 0x0002
#define CLEAR(x) (std::memset(&(x), 0, sizeof(x)))

namespace cap {
namespace {

std::string fourcc_to_string(unsigned int fmt) {
    char value[5] = {
        static_cast<char>(fmt & 0xff),
        static_cast<char>((fmt >> 8) & 0xff),
        static_cast<char>((fmt >> 16) & 0xff),
        static_cast<char>((fmt >> 24) & 0xff),
        '\0',
    };
    return value;
}

bool camera_debug_enabled() {
    const char* value = std::getenv("CAP_CAMERA_DEBUG");
    return value && value[0] && std::string(value) != "0";
}

void log_errno(const char* step) {
    std::cerr << "[camera] " << step << " failed: errno=" << errno << " (" << std::strerror(errno) << ")\n";
}

void log_debug(const std::string& message) {
    if (camera_debug_enabled()) std::cerr << message << "\n";
}

}  // namespace

V4L2Camera::V4L2Camera(const std::string& device, int width, int height)
    : device_(device), width_(width), height_(height) {}

V4L2Camera::~V4L2Camera() {
    stop();
    if (initialized_) release_buffers();
    if (fd_ >= 0) close(fd_);
}

bool V4L2Camera::init() {
    if (initialized_) return true;
    if (!init_device() || !set_format() || !request_buffers() || !queue_all_buffers()) return false;
    initialized_ = true;
    return true;
}

bool V4L2Camera::start() {
    if (!initialized_) return false;
    if (streaming_) return true;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) == -1) {
        log_errno("VIDIOC_STREAMON");
        return false;
    }
    log_debug("[camera] stream on");
    streaming_ = true;
    return true;
}

bool V4L2Camera::stop() {
    if (!streaming_) return true;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMOFF, &type) == -1) return false;
    streaming_ = false;
    return true;
}

bool V4L2Camera::get_frame(cv::Mat& frame) {
    if (!streaming_) return false;
    fd_set fds;
    struct timeval tv = {2, 0};
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    int ret = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret < 0) {
        log_errno("select");
        return false;
    }
    if (ret == 0) {
        std::cerr << "[camera] select timeout after 2s\n";
        return false;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[3];
    CLEAR(buf);
    CLEAR(planes);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = nplanes_;
    buf.m.planes = planes;
    if (ioctl(fd_, VIDIOC_DQBUF, &buf) == -1) {
        log_errno("VIDIOC_DQBUF");
        return false;
    }
    if (camera_debug_enabled()) {
        std::cerr << "[camera] dqbuf index=" << buf.index << " bytesused=";
        for (unsigned int j = 0; j < nplanes_; ++j) {
            std::cerr << (j ? "," : "") << planes[j].bytesused;
        }
        std::cerr << "\n";
    }

    bool ok = false;
    if (buf.index < BUFFER_COUNT && pixel_format_ == V4L2_PIX_FMT_BGR24 && nplanes_ >= 1) {
        packed_bgr_to_mat(static_cast<unsigned char*>(buffers_[buf.index].start[0]), frame);
        ok = !frame.empty();
    } else if (nplanes_ >= 3 && buf.index < BUFFER_COUNT) {
        yuv420m_to_bgr(static_cast<unsigned char*>(buffers_[buf.index].start[0]),
                       static_cast<unsigned char*>(buffers_[buf.index].start[1]),
                       static_cast<unsigned char*>(buffers_[buf.index].start[2]), frame);
        ok = !frame.empty();
    }

    if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1) {
        log_errno("VIDIOC_QBUF(requeue)");
        return false;
    }
    if (!ok) {
        std::cerr << "[camera] unsupported frame layout: fourcc=" << fourcc_to_string(pixel_format_)
                  << " nplanes=" << nplanes_ << " index=" << buf.index << "\n";
    }
    return ok;
}

bool V4L2Camera::init_device() {
    fd_ = open(device_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        log_errno("open camera");
        return false;
    }
    log_debug("[camera] opened " + device_);

    struct v4l2_input inp;
    CLEAR(inp);
    inp.index = 0;
    if (ioctl(fd_, VIDIOC_S_INPUT, &inp) == -1) log_errno("VIDIOC_S_INPUT");

    struct v4l2_streamparm parms;
    CLEAR(parms);
    parms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parms.parm.capture.timeperframe.numerator = 1;
    parms.parm.capture.timeperframe.denominator = 30;
    parms.parm.capture.capturemode = V4L2_MODE_VIDEO;
    if (ioctl(fd_, VIDIOC_S_PARM, &parms) == -1) log_errno("VIDIOC_S_PARM");
    return true;
}

bool V4L2Camera::set_format() {
    struct v4l2_format fmt;
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420M;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) == -1) {
        log_errno("VIDIOC_S_FMT YUV420M");
        CLEAR(fmt);
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width_;
        fmt.fmt.pix_mp.height = height_;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_BGR24;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) == -1) {
            log_errno("VIDIOC_S_FMT BGR24");
            return false;
        }
    }
    if (ioctl(fd_, VIDIOC_G_FMT, &fmt) == -1) {
        log_errno("VIDIOC_G_FMT");
        return false;
    }
    nplanes_ = fmt.fmt.pix_mp.num_planes;
    pixel_format_ = fmt.fmt.pix_mp.pixelformat;
    width_ = fmt.fmt.pix_mp.width;
    height_ = fmt.fmt.pix_mp.height;
    for (unsigned int i = 0; i < 3; ++i) bytes_per_line_[i] = fmt.fmt.pix_mp.plane_fmt[i].bytesperline;
    if (camera_debug_enabled()) {
        std::cerr << "[camera] format " << width_ << "x" << height_
                  << " fourcc=" << fourcc_to_string(pixel_format_)
                  << " nplanes=" << nplanes_ << " stride="
                  << bytes_per_line_[0] << "," << bytes_per_line_[1] << "," << bytes_per_line_[2] << "\n";
    }
    return nplanes_ >= 1 && width_ > 0 && height_ > 0;
}

bool V4L2Camera::request_buffers() {
    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) == -1) {
        log_errno("VIDIOC_REQBUFS");
        return false;
    }
    if (camera_debug_enabled()) {
        std::cerr << "[camera] reqbufs count=" << req.count << "\n";
    }

    for (unsigned int i = 0; i < BUFFER_COUNT; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[3];
        CLEAR(buf);
        CLEAR(planes);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = nplanes_;
        buf.m.planes = planes;
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) == -1) {
            log_errno("VIDIOC_QUERYBUF");
            return false;
        }
        for (unsigned int j = 0; j < nplanes_; ++j) {
            buffers_[i].length[j] = planes[j].length;
            buffers_[i].start[j] = mmap(nullptr, planes[j].length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd_, planes[j].m.mem_offset);
            if (buffers_[i].start[j] == MAP_FAILED) {
                log_errno("mmap");
                return false;
            }
            if (camera_debug_enabled()) {
                std::cerr << "[camera] mmap buffer=" << i << " plane=" << j
                          << " length=" << planes[j].length << "\n";
            }
        }
    }
    return true;
}

bool V4L2Camera::queue_all_buffers() {
    for (unsigned int i = 0; i < BUFFER_COUNT; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[3];
        CLEAR(buf);
        CLEAR(planes);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = nplanes_;
        buf.m.planes = planes;
        if (ioctl(fd_, VIDIOC_QBUF, &buf) == -1) {
            log_errno("VIDIOC_QBUF(queue)");
            return false;
        }
    }
    log_debug("[camera] queued buffers");
    return true;
}

void V4L2Camera::release_buffers() {
    for (auto& buffer : buffers_) {
        for (unsigned int j = 0; j < nplanes_; ++j) {
            if (buffer.start[j] && buffer.start[j] != MAP_FAILED) {
                munmap(buffer.start[j], buffer.length[j]);
                buffer.start[j] = nullptr;
            }
        }
    }
}

void V4L2Camera::packed_bgr_to_mat(unsigned char* data, cv::Mat& bgr_frame) {
    const int stride = bytes_per_line_[0] ? static_cast<int>(bytes_per_line_[0]) : width_ * 3;
    cv::Mat wrapped(height_, width_, CV_8UC3, data, stride);
    bgr_frame = wrapped.clone();
}

void V4L2Camera::yuv420m_to_bgr(unsigned char* y_plane, unsigned char* u_plane,
                                unsigned char* v_plane, cv::Mat& bgr_frame) {
    cv::Mat yuv_frame(height_ * 3 / 2, width_, CV_8UC1);
    std::memcpy(yuv_frame.data, y_plane, width_ * height_);
    unsigned char* uv_dst = yuv_frame.data + width_ * height_;
    const int uv_size = width_ * height_ / 4;
    std::memcpy(uv_dst, u_plane, uv_size);
    std::memcpy(uv_dst + uv_size, v_plane, uv_size);
    cv::cvtColor(yuv_frame, bgr_frame, cv::COLOR_YUV2BGR_I420);
}

std::vector<unsigned char> bgr_to_chw_rgb_320(const cv::Mat& bgr, cv::Mat* preview_rgb) {
    if (bgr.empty()) throw std::runtime_error("empty camera frame");
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    const int target = 320;
    const float scale = std::min(target / static_cast<float>(rgb.cols), target / static_cast<float>(rgb.rows));
    const int resized_w = std::max(1, static_cast<int>(std::round(rgb.cols * scale)));
    const int resized_h = std::max(1, static_cast<int>(std::round(rgb.rows * scale)));
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(resized_w, resized_h), 0, 0, cv::INTER_LINEAR);
    cv::Mat canvas(target, target, CV_8UC3, cv::Scalar(0, 0, 0));
    const int pad_x = (target - resized_w) / 2;
    const int pad_y = (target - resized_h) / 2;
    resized.copyTo(canvas(cv::Rect(pad_x, pad_y, resized_w, resized_h)));
    if (preview_rgb) *preview_rgb = canvas.clone();
    std::vector<unsigned char> tensor(target * target * 3);
    const int plane = target * target;
    for (int y = 0; y < target; ++y) {
        const cv::Vec3b* row = canvas.ptr<cv::Vec3b>(y);
        for (int x = 0; x < target; ++x) {
            const int idx = y * target + x;
            tensor[idx] = row[x][0];
            tensor[plane + idx] = row[x][1];
            tensor[2 * plane + idx] = row[x][2];
        }
    }
    return tensor;
}

}  // namespace cap
