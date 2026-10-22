#include "nopiskl/ai/yolov5.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Labels path missing");
        nopiskl::YoloV5Labels labels;
        labels.load(argv[1]);
        require(labels.name(0) == "person", "Incorrect COCO label");
        require(labels.name(-1) == "unknown", "Negative class index");
        require(labels.name(OBJ_CLASS_NUM) == "unknown", "Out-of-range class index");
        {
            nopiskl::YoloV5Labels independent;
            require(independent.name(0) == "unknown", "Labels must be owned by each instance");
            independent.load(argv[1]);
        }
        require(labels.name(0) == "person", "Destroying another instance must preserve labels");
        rknn_app_context_t ctx{};
        ctx.model_width = ctx.model_height = 640;
        std::array<rknn_tensor_attr, 3> attrs{};
        std::array<std::vector<int8_t>, 3> storage;
        std::array<rknn_tensor_mem, 3> mem{};
        std::array<rknn_tensor_mem *, 3> outputs{};
        for (int i = 0; i < 3; ++i) {
            auto &a = attrs[i];
            a.n_dims = 4;
            a.dims[0] = a.dims[1] = a.dims[2] = 1;
            a.dims[3] = 255;
            a.fmt = RKNN_TENSOR_NHWC;
            a.type = RKNN_TENSOR_INT8;
            a.zp = -128;
            a.scale = 1.0f / 128;
            storage[i].assign(255, -128);
            mem[i].virt_addr = storage[i].data();
            mem[i].size = 255;
            outputs[i] = &mem[i];
        }
        ctx.output_attrs = attrs.data();
        auto set = [&](int anchor, int cls, float confidence, float aw, float ah) {
            auto *cell = storage[0].data() + anchor * PROP_BOX_SIZE;
            auto quant = [](float f) { return static_cast<int8_t>(std::lround(f * 128) - 128); };
            cell[0] = cell[1] = quant(0.5f);
            cell[2] = quant(std::sqrt(10.0f / aw) / 2);
            cell[3] = quant(std::sqrt(13.0f / ah) / 2);
            cell[4] = quant(confidence);
            cell[5 + cls] = quant(0.95f);
        };
        set(0, 1, 0.8f, 10, 13);
        set(1, 0, 0.95f, 16, 30);
        set(2, 0, 0.7f, 33, 23);
        object_detect_result_list result{};
        require(post_process(&ctx, outputs.data(), 0.25f, 0.45f, &result) == 0, "Decode failed");
        require(result.count == 2, "Same-class suppression or different-class preservation failed");
        require(result.results[0].cls_id == 0 && result.results[1].cls_id == 1,
                "Score/class ordering failed");
        require(result.results[0].prop > result.results[1].prop, "Scores not sorted");
        mem[0].size = 16;
        require(post_process(&ctx, outputs.data(), 0.25f, 0.45f, &result) < 0,
                "Short tensor buffer accepted");
        bool rejected = false;
        try {
            labels.load("/nonexistent/nopiskl-labels");
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "Missing labels accepted");
        require(labels.name(0) == "person", "Failed label reload must preserve existing labels");
        std::cout
            << "PASS: COCO labels, quantized decode, class-aware NMS, sorting, tensor bounds\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
