# 扩展指南

## 文件与模块约定

功能模块统一放在 `apps/SmartIPC/modules/`。新增 C++ 实现使用 `.cpp`。对外接口放在模块 `include/nopiskl/<模块>/`，内部辅助头文件放在 `src/`。跨模块使用完整包含路径，例如 `#include "nopiskl/control/control.h"`，不要公开 `src/` 包含目录。

在所属模块的 `CMakeLists.txt` 登记源文件和实际需要的依赖，使用 `nopiskl_enable_warnings()` 统一编译检查。新增模块在 `apps/SmartIPC/CMakeLists.txt` 按依赖顺序接入。供应商依赖通过 `cmake/Dependencies.cmake` 中的目标接入。公共头文件需要能独立包含，主机完整构建会自动检查。

## 新增媒体后端

实现 `nopiskl/media/media.h` 的 `Media` 接口，代码放在 `apps/SmartIPC/modules/media/src/backends/`。在应用组装层创建实现并交给 Control；若作为默认平台后端，提供 `factory.h` 声明的 `make_media`，并在媒体模块 CMake 中选择它。Control 仅依赖接口，不应加入 Rockchip/OpenCV 头文件。

后端的 configure/step/snapshot 在控制工作线程执行，析构会在工作线程结束后进行。新增后端应保持有限等待、错误状态和资源释放契约。可参考 `tests/test_control.cpp` 注入测试替身验证行为。

## 修改配置或检测模型

配置字段及取值规则在 `modules/config` 中维护（相对 `apps/SmartIPC/`）；应用和示例通过 `nopiskl/config/config.h` 读取配置。配置应用、持久化与回滚统一经过控制模块内部的 `ConfigService`，不要在 HTTP 路由直接重建媒体通道或写配置文件。

检测模型由 `nopiskl/ai/detector.h` 中的 `YoloV5Detector` 管理。媒体只提供同步借用的 BGR 图像，读取还原到原图坐标的检测结果；RKNN 内存操作和预处理放在 AI 模块。替换模型时需一起适配输出布局、类别数、标签、固定 anchors 和前后处理，当前版本不支持通过外部 anchors 文件更换解码参数。标签是实例数据，避免引入可变全局模型状态。

## 新增业务命令

1. 在 `apps/SmartIPC/modules/control` 定义命令字段、参数范围、完成语义和错误码。
2. 在内部 `TaskQueue` 登记任务类型，在 Control 工作线程执行硬件相关操作，保持队列和任务历史有界。
3. 在 Server 路由解析请求，保持鉴权和 CSRF 检查。
4. 在 `/capabilities` 声明能力，页面按能力启用控件。
5. 为新增字段和失败行为补充 API 文档及主机测试，板端功能另做实测。

## 新增传输协议

HTTP 只是适配层。串口或 TCP 子程序可调用 `Control::submit_motor/submit_config/submit_snapshot`，使用相同任务模型。截图结果的下载 URL 由各传输适配层生成，控制层仅返回标识与元数据。对外协议应包含版本、请求序号、长度上限及适合介质的校验机制；网络入口仍需鉴权，不直接暴露 MPI 调用。

## 新增电机后端

实现 `nopiskl/control/motor.h` 的 `Motor` 接口，在应用组装层创建并传入 Control，或扩展默认 `make_motor` 工厂。模拟接口只使用归一化速度和持续时间，真实驱动须补充动作映射、自动停止、反馈、限位和退出释放。详见 `motor_protocol.md`。

## 新增编码格式

扩展配置校验、VENC 属性、RTSP 会话 codec 和 capabilities。编码器与会话必须一致。先完成板端推流回归，再在页面开放格式选择。当前版本返回 unsupported_codec，避免只修改页面而没有底层能力。

## 新增截图模式

扩展 snapshot 配置与校验，将任务交给媒体所有者处理。若增加周期存储，需要限定任务并发、磁盘容量、保存目录、命名策略和闪存写入频率；不要让 HTTP 用户直接指定任意设备文件路径。

## 替换 Web 页面

静态文件位于 `apps/SmartIPC/web`。仅 `/`、`/app.js`、`/style.css` 三个资源公开；新增资源需显式加入 Server 白名单。默认 CSP 限制同源脚本及样式，使用独立 JS/CSS 文件，页面使用 textContent 显示设备返回的文本。
