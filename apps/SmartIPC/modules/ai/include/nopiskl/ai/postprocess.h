#ifndef _RKNN_YOLOV5_DEMO_POSTPROCESS_H_
#define _RKNN_YOLOV5_DEMO_POSTPROCESS_H_

#include <array>
#include <string>

#define OBJ_NAME_MAX_SIZE 64
#define OBJ_NUMB_MAX_SIZE 128
#define OBJ_CLASS_NUM 80
#define PROP_BOX_SIZE (5 + OBJ_CLASS_NUM)

// 前置声明使该头文件可以独立包含，不要求先包含 yolov5.h。
struct rknn_app_context_t;
typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} image_rect_t;

typedef struct {
    image_rect_t box;
    float prop;
    int cls_id;
} object_detect_result;

typedef struct {
    int id;
    int count;
    object_detect_result results[OBJ_NUMB_MAX_SIZE];
} object_detect_result_list;

namespace nopiskl {
// 标签随检测器实例持有，避免加载或销毁一个模型影响另一个实例。
class YoloV5Labels {
public:
    void load(const std::string &path);
    const std::string &name(int class_id) const;

private:
    std::array<std::string, OBJ_CLASS_NUM> labels_;
};
} // namespace nopiskl

int post_process(rknn_app_context_t *app_ctx, void *outputs, float conf_threshold,
                 float nms_threshold, object_detect_result_list *od_results);

#endif //_RKNN_YOLOV5_DEMO_POSTPROCESS_H_
