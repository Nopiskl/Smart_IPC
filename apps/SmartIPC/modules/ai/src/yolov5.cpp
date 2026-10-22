// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
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
#include <cstdlib>
#include <cstring>

int release_yolov5_model(rknn_app_context_t *ctx) {
    if (!ctx) {
        return 0;
    }
    for (auto &mem : ctx->input_mems) {
        if (mem) {
            rknn_destroy_mem(ctx->rknn_ctx, mem);
        }
        mem = nullptr;
    }
    for (auto &mem : ctx->output_mems) {
        if (mem) {
            rknn_destroy_mem(ctx->rknn_ctx, mem);
        }
        mem = nullptr;
    }
    free(ctx->input_attrs);
    free(ctx->output_attrs);
    if (ctx->rknn_ctx) {
        rknn_destroy(ctx->rknn_ctx);
    }
    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int init_yolov5_model(const char *path, rknn_app_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    // 任意失败均销毁已分配的张量内存和 RKNN 上下文。
    auto fail = [ctx]() {
        release_yolov5_model(ctx);
        return -1;
    };
    if (rknn_init(&ctx->rknn_ctx, const_cast<char *>(path), 0, 0, nullptr) < 0) {
        return fail();
    }
    if (rknn_query(ctx->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &ctx->io_num, sizeof(ctx->io_num)) < 0) {
        return fail();
    }
    // 当前后处理只适配单输入、三输出的量化 COCO YOLOv5 模型。
    if (ctx->io_num.n_input != 1 || ctx->io_num.n_output != 3) {
        return fail();
    }
    ctx->input_attrs = static_cast<rknn_tensor_attr *>(calloc(1, sizeof(rknn_tensor_attr)));
    ctx->output_attrs = static_cast<rknn_tensor_attr *>(calloc(3, sizeof(rknn_tensor_attr)));
    if (!ctx->input_attrs || !ctx->output_attrs) {
        return fail();
    }
    auto &input = ctx->input_attrs[0];
    if (rknn_query(ctx->rknn_ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &input, sizeof(input)) < 0) {
        return fail();
    }
    if (input.n_dims != 4 || input.dims[0] != 1 || input.fmt != RKNN_TENSOR_NHWC) {
        return fail();
    }
    ctx->model_height = input.dims[1];
    ctx->model_width = input.dims[2];
    ctx->model_channel = input.dims[3];
    if (ctx->model_channel != 3 || ctx->model_width <= 0 || ctx->model_height <= 0) {
        return fail();
    }
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    ctx->input_mems[0] = rknn_create_mem(ctx->rknn_ctx, input.size_with_stride);
    if (!ctx->input_mems[0] || rknn_set_io_mem(ctx->rknn_ctx, ctx->input_mems[0], &input) < 0) {
        return fail();
    }
    for (int i = 0; i < 3; ++i) {
        auto &output = ctx->output_attrs[i];
        output.index = i;
        if (rknn_query(ctx->rknn_ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &output, sizeof(output)) <
            0) {
            return fail();
        }
        if (output.n_dims != 4 || output.type != RKNN_TENSOR_INT8 ||
            output.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC || output.dims[1] == 0 ||
            output.dims[2] == 0) {
            return fail();
        }
        ctx->output_mems[i] = rknn_create_mem(ctx->rknn_ctx, output.size_with_stride);
        if (!ctx->output_mems[i] ||
            rknn_set_io_mem(ctx->rknn_ctx, ctx->output_mems[i], &output) < 0) {
            return fail();
        }
    }
    return 0;
}

int inference_yolov5_model(rknn_app_context_t *ctx, object_detect_result_list *results,
                           float confidence, float nms) {
    memset(results, 0, sizeof(*results));
    int ret = rknn_run(ctx->rknn_ctx, nullptr);
    if (ret < 0) {
        return ret;
    }
    for (auto *mem : ctx->output_mems) {
        ret = rknn_mem_sync(ctx->rknn_ctx, mem, RKNN_MEMORY_SYNC_FROM_DEVICE);
        if (ret < 0) {
            return ret;
        }
    }
    return post_process(ctx, ctx->output_mems, confidence, nms, results);
}
