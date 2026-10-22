// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "nopiskl/ai/yolov5.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// 当前内置模型的固定 anchors；与三路 COCO YOLOv5 输出布局一起维护。
constexpr int anchors[3][6] = {
    {10, 13, 16, 30, 33, 23}, {30, 61, 62, 45, 59, 119}, {116, 90, 156, 198, 373, 326}};
struct Candidate {
    float x, y, width, height, score;
    int cls;
};
float overlap(const Candidate &a, const Candidate &b) {
    float w = std::max(0.0f, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
    float h = std::max(0.0f, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
    float intersection = w * h, area = a.width * a.height + b.width * b.height - intersection;
    return area > 0 ? intersection / area : 0;
}
int clip(float value, int maximum) {
    return static_cast<int>(std::max(0.0f, std::min(value, static_cast<float>(maximum))));
}
} // namespace

void nopiskl::YoloV5Labels::load(const std::string &path) {
    std::array<std::string, OBJ_CLASS_NUM> next;
    std::ifstream file(path);
    for (auto &label : next) {
        if (!std::getline(file, label)) {
            throw std::runtime_error("Cannot read 80 YOLOv5 labels from " + path);
        }
        if (!label.empty() && label.back() == '\r') {
            label.pop_back();
        }
        if (label.empty() || label.size() >= OBJ_NAME_MAX_SIZE) {
            throw std::runtime_error("Invalid YOLOv5 label in " + path);
        }
    }
    labels_ = std::move(next);
}
const std::string &nopiskl::YoloV5Labels::name(int id) const {
    static const std::string unknown = "unknown";
    return id >= 0 && id < OBJ_CLASS_NUM && !labels_[id].empty() ? labels_[id] : unknown;
}

int post_process(rknn_app_context_t *ctx, void *outputs, float confidence, float nms,
                 object_detect_result_list *results) {
    std::memset(results, 0, sizeof(*results));
    auto **memory = static_cast<rknn_tensor_mem **>(outputs);
    std::vector<Candidate> candidates;
    candidates.reserve(256);
    // 三个尺度均为 NHWC INT8。按属性读取通道和行跨度，不固定模型输入尺寸。
    for (int branch = 0; branch < 3; ++branch) {
        const auto &attr = ctx->output_attrs[branch];
        if (attr.n_dims != 4 || attr.dims[0] != 1 || attr.fmt != RKNN_TENSOR_NHWC ||
            attr.type != RKNN_TENSOR_INT8 || attr.scale <= 0 || !memory[branch] ||
            !memory[branch]->virt_addr) {
            return -1;
        }
        int height = attr.dims[1], width = attr.dims[2], channels = attr.dims[3];
        if (height <= 0 || width <= 0 || channels < 3 * PROP_BOX_SIZE) {
            return -1;
        }
        size_t row_pixels = attr.w_stride ? attr.w_stride : static_cast<unsigned>(width);
        if (row_pixels < static_cast<size_t>(width) ||
            row_pixels * height * channels > memory[branch]->size) {
            return -1;
        }
        const auto *data = static_cast<const int8_t *>(memory[branch]->virt_addr);
        auto dequant = [&](int8_t value) {
            return (static_cast<int>(value) - attr.zp) * attr.scale;
        };
        float stride_x = static_cast<float>(ctx->model_width) / width;
        float stride_y = static_cast<float>(ctx->model_height) / height;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                for (int anchor = 0; anchor < 3; ++anchor) {
                    const auto *cell =
                        data + (y * row_pixels + x) * channels + anchor * PROP_BOX_SIZE;
                    float objectness = dequant(cell[4]);
                    if (objectness < confidence) {
                        continue;
                    }
                    int cls = 0;
                    for (int i = 1; i < OBJ_CLASS_NUM; ++i) {
                        if (cell[5 + i] > cell[5 + cls]) {
                            cls = i;
                        }
                    }
                    float score = objectness * dequant(cell[5 + cls]);
                    if (score < confidence) {
                        continue;
                    }
                    float bw = dequant(cell[2]) * 2, bh = dequant(cell[3]) * 2;
                    bw = bw * bw * anchors[branch][2 * anchor];
                    bh = bh * bh * anchors[branch][2 * anchor + 1];
                    if (bw <= 0 || bh <= 0) {
                        continue;
                    }
                    float cx = (dequant(cell[0]) * 2 - 0.5f + x) * stride_x;
                    float cy = (dequant(cell[1]) * 2 - 0.5f + y) * stride_y;
                    candidates.push_back({cx - bw / 2, cy - bh / 2, bw, bh, score, cls});
                }
            }
        }
    }
    // 限制 NMS 的二次复杂度，优先处理置信度最高的 1024 个候选框。
    constexpr size_t max_candidates = 1024;
    auto descending = [](const Candidate &a, const Candidate &b) { return a.score > b.score; };
    if (candidates.size() > max_candidates) {
        std::partial_sort(candidates.begin(), candidates.begin() + max_candidates, candidates.end(),
                          descending);
        candidates.resize(max_candidates);
    } else {
        std::sort(candidates.begin(), candidates.end(), descending);
    }
    std::vector<Candidate> selected;
    selected.reserve(OBJ_NUMB_MAX_SIZE);
    for (const auto &candidate : candidates) {
        bool suppressed = false;
        for (const auto &previous : selected) {
            // 比较排序后的实际类别，避免原示例中按排序前索引跨类别抑制。
            if (candidate.cls == previous.cls && overlap(candidate, previous) > nms) {
                suppressed = true;
                break;
            }
        }
        if (suppressed) {
            continue;
        }
        int left = clip(candidate.x, ctx->model_width), top = clip(candidate.y, ctx->model_height);
        int right = clip(candidate.x + candidate.width, ctx->model_width),
            bottom = clip(candidate.y + candidate.height, ctx->model_height);
        if (right <= left || bottom <= top) {
            continue;
        }
        auto &out = results->results[results->count++];
        out.box = {left, top, right, bottom};
        out.prop = candidate.score;
        out.cls_id = candidate.cls;
        selected.push_back(candidate);
        if (results->count == OBJ_NUMB_MAX_SIZE) {
            break;
        }
    }
    return 0;
}
