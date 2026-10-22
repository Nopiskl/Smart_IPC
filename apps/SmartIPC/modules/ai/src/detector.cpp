#include "nopiskl/ai/detector.h"
#include "nopiskl/ai/yolov5.h"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace nopiskl {
struct YoloV5Detector::Impl {
    rknn_app_context_t model{};
    YoloV5Labels labels;
    cv::Mat letterbox, resized;
    std::string model_path, labels_path;

    ~Impl() {
        release_yolov5_model(&model);
    }
};
YoloV5Detector::YoloV5Detector() = default;
YoloV5Detector::~YoloV5Detector() = default;
void YoloV5Detector::reset() noexcept {
    impl_.reset();
}
void YoloV5Detector::load(const std::string &model_path, const std::string &labels_path) {
    if (impl_ && impl_->model_path == model_path && impl_->labels_path == labels_path) {
        return;
    }
    reset();
    auto next = std::make_unique<Impl>();
    next->labels.load(labels_path);
    if (init_yolov5_model(model_path.c_str(), &next->model) != 0) {
        throw std::runtime_error("Cannot load YOLOv5 model: " + model_path);
    }
    next->model_path = model_path;
    next->labels_path = labels_path;
    impl_ = std::move(next);
}
void YoloV5Detector::detect(const BgrImageView &image, float confidence, float nms,
                            std::vector<Detection> &results) {
    results.clear();
    if (!impl_) {
        throw std::logic_error("YOLOv5 detector is not loaded");
    }
    if (!image.data || image.width <= 0 || image.height <= 0) {
        throw std::invalid_argument("Invalid BGR image");
    }
    const size_t row_bytes = static_cast<size_t>(image.width) * 3;
    if (image.row_stride < row_bytes || image.size < row_bytes ||
        static_cast<size_t>(image.height - 1) > (image.size - row_bytes) / image.row_stride) {
        throw std::invalid_argument("BGR image buffer is too small");
    }
    if (!std::isfinite(confidence) || !std::isfinite(nms) || confidence <= 0 || confidence >= 1 ||
        nms <= 0 || nms >= 1) {
        throw std::invalid_argument("Invalid detection thresholds");
    }
    auto &model = impl_->model;
    const int mw = model.model_width, mh = model.model_height;
    const float scale =
        std::min(static_cast<float>(mw) / image.width, static_cast<float>(mh) / image.height);
    const int sw = std::max(1, static_cast<int>(image.width * scale));
    const int sh = std::max(1, static_cast<int>(image.height * scale));
    const int left = (mw - sw) / 2, top = (mh - sh) / 2;
    // OpenCV 的外部缓冲构造函数需要非 const 指针，后续操作只读取 borrowed。
    const cv::Mat borrowed(image.height, image.width, CV_8UC3,
                           const_cast<unsigned char *>(image.data), image.row_stride);
    impl_->letterbox.create(mh, mw, CV_8UC3);
    impl_->letterbox.setTo(cv::Scalar(0, 0, 0));
    cv::resize(borrowed, impl_->resized, cv::Size(sw, sh));
    cv::cvtColor(impl_->resized, impl_->resized, cv::COLOR_BGR2RGB);
    impl_->resized.copyTo(impl_->letterbox(cv::Rect(left, top, sw, sh)));

    const size_t stride =
        static_cast<size_t>(model.input_attrs[0].w_stride ? model.input_attrs[0].w_stride : mw) * 3;
    auto *memory = model.input_mems[0];
    if (stride < static_cast<size_t>(mw) * 3 || static_cast<size_t>(mh) > memory->size / stride) {
        throw std::runtime_error("Unsupported RKNN input stride");
    }
    auto *target = static_cast<unsigned char *>(memory->virt_addr);
    for (int row = 0; row < mh; ++row) {
        std::memcpy(target + row * stride, impl_->letterbox.ptr(row), mw * 3);
    }
    if (rknn_mem_sync(model.rknn_ctx, memory, RKNN_MEMORY_SYNC_TO_DEVICE) != 0) {
        throw std::runtime_error("RKNN input sync failed");
    }
    object_detect_result_list decoded{};
    if (inference_yolov5_model(&model, &decoded, confidence, nms) != 0) {
        throw std::runtime_error("YOLOv5 inference failed");
    }
    results.reserve(OBJ_NUMB_MAX_SIZE);
    auto restore = [scale](int value, int padding, int limit) {
        return std::max(0, std::min(limit - 1, static_cast<int>((value - padding) / scale)));
    };
    for (int i = 0; i < decoded.count; ++i) {
        const auto &d = decoded.results[i];
        results.push_back(
            {restore(d.box.left, left, image.width), restore(d.box.top, top, image.height),
             restore(d.box.right, left, image.width), restore(d.box.bottom, top, image.height),
             d.prop, d.cls_id, impl_->labels.name(d.cls_id)});
    }
}
} // namespace nopiskl
