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

#ifndef _RKNN_DEMO_YOLOV5_H_
#define _RKNN_DEMO_YOLOV5_H_

#include "rknn_api.h"

typedef struct rknn_app_context_t {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
#if defined(RV1106_1103)
    rknn_tensor_mem *input_mems[1];
    rknn_tensor_mem *output_mems[3];
#endif
    int model_channel;
    int model_width;
    int model_height;
} rknn_app_context_t;

#include "nopiskl/ai/postprocess.h"

int init_yolov5_model(const char *model_path, rknn_app_context_t *app_ctx);

int release_yolov5_model(rknn_app_context_t *app_ctx);

int inference_yolov5_model(rknn_app_context_t *app_ctx, object_detect_result_list *od_results,
                           float confidence, float nms);

#endif //_RKNN_DEMO_YOLOV5_H_
