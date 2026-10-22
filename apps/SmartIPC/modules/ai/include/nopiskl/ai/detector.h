#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace nopiskl {
// 同步调用期间借用的 BGR 图像；调用方保留缓冲区所有权。
struct BgrImageView {
    const unsigned char *data;
    int width, height;
    size_t row_stride, size;
};
struct Detection {
    int left, top, right, bottom;
    float confidence;
    int class_id;
    std::string label;
};
// 模型、预处理缓存和 RKNN 张量由检测器管理；公开接口不包含 SDK/OpenCV 类型。
class YoloV5Detector {
public:
    YoloV5Detector();
    ~YoloV5Detector();
    void load(const std::string &model_path, const std::string &labels_path);
    void reset() noexcept;
    void detect(const BgrImageView &image, float confidence, float nms,
                std::vector<Detection> &results);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nopiskl
