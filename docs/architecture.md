# SmartIPC 架构

工程以应用层、通用服务和平台媒体实现分层，应用源码与功能模块统一归属 `apps/SmartIPC/`。基础示例通过库目标复用媒体实现，SmartIPC 额外组装控制层及 Web 服务。

```text
浏览器 / 外部客户端
        │ HTTP + JSON /api/v1
        ▼
Server → Auth（会话、CSRF、登录限速）
        │ 参数校验
        ▼
Control（设备操作入口与单个工作线程）
        ├─ TaskQueue：有界命令队列、优先停止、任务历史
        ├─ ConfigService：应用 → 文件保存；失败时尝试恢复原配置
        ├─ SnapshotStore：最新一张截图及共享下载引用
        ├─ Media：ISP / VI → OpenCV → YoloV5Detector → VENC → RTSP
        └─ Motor：disabled / simulated / 后续硬件后端

YoloV5Detector：模型与标签 → letterbox / RGB → RKNN → 解码 / NMS → 坐标还原
```

## 源码与构建边界

项目自有实现文件统一为 `.cpp`，公开接口为 `.h`。功能模块放在 `apps/SmartIPC/modules/`；每个模块的头文件放在 `include/nopiskl/<模块>/`，源文件放在 `src/`。模块的 `src/` 不加入公开包含路径。

```text
apps/SmartIPC/
    CMakeLists.txt           内部模块与主程序的构建入口
    src/
        main.cpp             信号注册、顶层异常处理
        application.cpp/.h   读取配置、创建后端、组装服务、管理生命周期
        options.cpp/.h       命令行参数及帮助
        admin_setup.cpp/.h   初始密码输入、终端回显和密码缓冲清理
    config/                  默认配置
    web/                     管理页面静态资源
    modules/
        common/
            include/nopiskl/common/{types,files,system}.h
            src/{files,system}.cpp
        config/
            include/nopiskl/config/config.h
            src/config.cpp  SmartIPC 配置加载和校验
        ai/
            include/nopiskl/ai/{detector,yolov5,postprocess}.h
            src/{detector,yolov5,postprocess}.cpp
        media/
            include/nopiskl/media/{media,factory}.h
            src/backends/{rk_media,simulated_media}.cpp
        control/
            include/nopiskl/control/{control,motor}.h
            src/{control,motor}.cpp
            src/config_service.cpp/.h  配置应用、保存、回滚
            src/task_queue.cpp/.h      队列与任务记录
            src/snapshot_store.cpp/.h  单张截图缓存
        web/
            include/nopiskl/web/{auth,server}.h
            src/{auth,server,http_utils}.cpp
            src/http_utils.h HTTP 读写与 JSON 解析的私有辅助接口
examples/common/             基础示例共用的启动循环
cmake/
    ProjectOptions.cmake     语言标准、统一告警、编译数据库
    Dependencies.cmake       第三方库、平台路径与编译定义
    Install.cmake            部署文件和运行库安装规则
    toolchains/              交叉编译器选择
```

根 `CMakeLists.txt` 负责公共依赖、SmartIPC、示例、测试和安装的组织；`apps/SmartIPC/CMakeLists.txt` 按依赖顺序添加内部模块，再创建主程序目标。各模块维护自己的源文件列表、公开包含路径与链接依赖。示例和测试直接链接这些库目标，不依赖 SmartIPC 可执行文件。`build/<profile>/compile_commands.json` 可供 IDE 和代码分析工具使用。

| 构建目标 | 依赖与职责 |
| --- | --- |
| `nopiskl_common` | JSON 类型、文件和系统函数，无配置规则、媒体或 HTTP 依赖 |
| `nopiskl_config` | SmartIPC 配置加载、字段校验和参数约束，依赖 common |
| `nopiskl_media_api` | 仅公开媒体接口，依赖 common，不链接板端库 |
| `nopiskl_postprocess` | YOLOv5 后处理；只需 RKNN 张量声明，可在主机测试 |
| `nopiskl_ai` | 检测器封装；私有链接后处理、RKNN 运行库和 OpenCV，不引入 MPI/ISP 依赖 |
| `nopiskl_media` | 根据构建模式选择板端或模拟实现；板端私有链接 AI、Rockchip 和 OpenCV |
| `nopiskl_control` | 设备协调及内部配置事务、任务队列、截图缓存；依赖媒体接口、配置和公共函数，不链接具体媒体实现 |
| `nopiskl_web` | 提供 Auth 与 Server；HTTP 库、密码库和控制服务为私有链接依赖 |
| `SmartIPC` | 应用组装点，将具体媒体和电机对象交给 Control，再接入 Server |

## 后端所有权与应用组装

`Control` 构造函数接收 `std::unique_ptr<Media>` 和 `std::unique_ptr<Motor>`，并接管它们的所有权。空后端立即报错。控制层不调用媒体工厂；选择真实硬件、主机模拟或测试替身的工作放在 `application.cpp`。

```cpp
Auth auth(options.data);
Control control(config, options.data, make_media(true), make_motor(options.simulate_motor));
Server server(control, auth, options.web);
```

`media.h` 定义操作接口，`factory.h` 声明默认后端创建函数。电机接口和默认工厂在 `motor.h` 中。后续硬件驱动也可以由应用直接构造并传入 Control，不必修改排队和任务查询逻辑。

`Control` 的公开头文件通过私有实现指针隐藏队列、互斥锁、线程和缓存；内部组件放在模块 `src/`，不增加公开 API。`TaskQueue` 使用枚举区分内部任务类型，统一维护排队及历史淘汰；`SnapshotStore` 用单个共享指针持有最新截图。两者由 Control 的任务锁保护，设备状态与任务成功结果同时发布。

`Server` 的公开头文件只前置声明 Auth、Control 和 HTTP 上下文；HTTP 报文解析、Cookie 提取及响应写入位于私有 `http_utils`，请求分发留在 `server.cpp`。控制层返回截图 ID、格式和大小，Server 在任务响应中补充 HTTP 下载 URL。配置加载与产品参数规则位于独立 `config` 模块，跨模块通过 `nopiskl/config/config.h` 使用；`common` 提供文件及系统工具。内存信息采集在任务锁之外执行。

主机完整构建会逐个编译 13 个公开头文件，检查其能独立包含；控制层测试仅链接 `nopiskl_control`、配置及接口依赖，用注入的后端验证任务失败恢复、线程所有权、配置保存、截图缓存与退出停止，不需要具体媒体实现或 HTTP 库。

## 检测器边界

`YoloV5Detector` 的公开接口只包含 `BgrImageView` 和 `Detection` 等标准 C++ 类型。媒体后端借出当前图像供同步推理使用，检测器管理模型上下文、输入输出张量和预处理缓存，返回原图坐标中的检测结果及标签；框和文字的绘制仍由媒体层负责。OpenCV 依赖通过单独的 CMake 目标封装，AI 模块无需链接 ISP、VI 或 VENC。

标签改为 `YoloV5Labels` 实例成员。加载另一实例、销毁另一实例或标签重载失败均不会清空已有实例的标签。当前仍只支持随包的三输出、80 类 COCO YOLOv5 模型；固定 anchors 与张量解码共同维护在 `ai/src/postprocess.cpp`。原 `models/yolov5/anchors_yolov5.txt` 未被读取，已移除，避免出现两个参数来源。

## 线程与资源

HTTP 使用 CivetWeb，固定 2 个处理线程，关闭 keep-alive；请求头和 JSON body 分别限制为 8192 字节，JSON 嵌套最多 8 层。HTTP 处理线程不直接操作 MPI、RKNN 或电机。

Control 工作线程独占 Media 和 Motor，视频参数修改、截图编码和电机命令在该线程串行执行。队列容纳 16 个普通待执行任务，停止命令可以额外优先入队并取消尚未执行的电机命令。保留最近 64 个任务及最新一张截图。

媒体使用 2 个 VI 缓冲、1 个 RGB DMA 输出块和 2 个编码码流缓冲。OpenCV 缓存按需要复用；截图按请求编码。Argon2id 登录计算临时使用 8 MiB，Auth 的互斥锁避免多个哈希同时占用内存。线程栈、RKNN、驱动和图像缓存也会占用内存，实际总量需在目标板测量。

## 配置一致性

`GET .../config` 始终返回已生效配置。`PUT` 做即时校验后返回 202 及任务 ID；排队中、运行中任务可从 `device/status.pending_tasks` 获取。执行阶段以当时的最新配置合并 patch，避免并发请求互相覆盖其他字段。

视频配置重建媒体通道；AI 阈值和截图选项在工作线程直接切换。`ConfigService::apply` 在该线程串行完成最新配置合并、校验、媒体应用、文件保存和失败回滚；查询通过独立互斥锁读取生效配置。新配置成功应用并完成文件写入后才发布到查询接口。应用或保存失败时尝试恢复原配置；若硬件恢复也失败，设备状态报告媒体不可用及错误，任务仍明确失败。保存使用同目录临时文件、fsync、rename，配置和凭据不通过静态文件服务暴露。

## 生命周期

MPI SYS 初始化先于 DMA 内存申请。VI 帧及 VENC 流以局部资源守卫配对释放。退出时先停止 HTTP 接收，再等待媒体线程结束，最后销毁媒体及 NPU 资源。VI/VENC 等待使用有限超时；OpenCV VideoCapture 的底层等待行为仍取决于供应商库。

程序不自动执行系统服务停止脚本。部署者在启动前处理摄像头占用，避免程序启动时隐式更改系统服务状态。
