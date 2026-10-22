# 第三方组件及来源

本工程的项目品牌与新增模块为 **nopiskl / SmartIPC**。它基于既有示例整理，不将供应商代码、模型或二进制库声明为原创。

| 组件 | 来源与说明 |
| --- | --- |
| 基础示例与平台封装 | 本工作区 `Video_examples/luckfox_pico_rkmpi_example`；上游 LuckfoxTECH `luckfox_pico_rkmpi_example`，原平台封装署名 Luckfox team。重构后作为项目媒体模块使用 |
| Rockchip MPI、RKAIQ、RKNN、RGA、RTSP、预编译库 | 原工程所附 Rockchip/Luckfox SDK 依赖；各头文件保留原始版权，二进制库与固件的使用条件以供应商发布内容为准 |
| YOLOv5 模型加载及后处理 | 原示例中 Rockchip Electronics Co., Ltd. 2021/2023 代码；Apache-2.0 原始授权头保留，已修改生命周期、标签加载和 NMS 索引 |
| RKNN 模型与 COCO 标签 | 原示例提供，位于 `models/yolov5`；未改变模型内容，模型及数据集使用条件以其原始发布为准 |
| OpenCV-Mobile | 原工程所附 4.10.0 静态库及头文件；OpenCV/nihui 原始授权头保留，包含 stb 图像编解码实现 |
| CivetWeb 1.16 | https://github.com/civetweb/civetweb/tree/v1.16 ，MIT；授权见 `third_party/civetweb/LICENSE.md` |
| Monocypher 4.0.2 | https://github.com/LoupVaillant/Monocypher/tree/4.0.2 ，BSD-2-Clause 或 CC0 双重授权，源文件包含完整说明 |
| nlohmann/json 3.11.3 | https://github.com/nlohmann/json/tree/v3.11.3 ，MIT；授权保留在 `third_party/json/json.hpp` |

供应商目录及来源说明中的上游名称用于准确标识依赖。可维护的工程目标、界面品牌、构建变量和日志前缀统一为 nopiskl / SmartIPC。`cmake/toolchains/uclibc.cmake` 中相邻 SDK 的默认路径对应工作区实际目录，并可通过 `NOPISKL_SDK_PATH` 覆盖。

CivetWeb v1.16 下载归档 SHA-256：`f0e471c1bf4e7804a6cfb41ea9d13e7d623b2bcc7bc1e2a4dd54951a24d60285`。新增依赖已经固定在本目录，构建不联网拉取。
