# nopiskl Video / SmartIPC

基于 RV1106、Rockchip MPI、RKNN 和 OpenCV-Mobile 的嵌入式视频工程。主应用 **SmartIPC** 提供 YOLOv5 目标检测、检测结果叠加、H.264 RTSP 推流、设备管理页面、配置保存、按需截图和外部控制 API。

本目录从 `../Video_examples/` 独立整理而来，原目录保持独立。项目维护名称为 nopiskl，第三方来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 应用入口

| 目标 | 功能 |
| --- | --- |
| `SmartIPC` | 检测、推流、管理页面、HTTP API、截图及电机扩展入口 |
| `nopiskl_rtsp_opencv` | MPI 采集、OpenCV FPS 叠加、硬件编码和 RTSP |
| `nopiskl_rtsp_opencv_capture` | OpenCV VideoCapture 采集、FPS 叠加、编码和 RTSP |

两个 RetinaFace 示例及其模型、构建入口已从本工程移除。基础示例与 SmartIPC 共享媒体实现，三者分别运行，避免同时占用摄像头。

## 快速编译

需要 Linux、CMake 3.16+、make 和对应 ARM hard-float 工具链。第三方源码及目标库已经包含，构建不需要下载依赖。

```bash
export NOPISKL_SDK_PATH=/home/alientek/RV1106/SDK/luckfox-pico
./build.sh --target all --libc uclibc --jobs 2
```

产物位于 `dist/uclibc/`。仅构建主应用可使用 `--target SmartIPC`。

```bash
# glibc 需要另外准备与板端 ABI 匹配的工具链
export NOPISKL_GLIBC_PREFIX=/opt/arm/bin/arm-linux-gnueabihf-
./build.sh --target all --libc glibc
```

## 板端运行

将完整部署目录复制到设备的可写分区，例如 `/userdata/SmartIPC`。先停止正在占用摄像头、ISP、编码器或 554 端口的原 IPC 服务。原系统通常提供 `RkLunch-stop.sh`，具体以板端固件为准。

```sh
cd /userdata/SmartIPC
# 首次运行，在终端交互设置密码；用户名为 admin
./start.sh --init-password
./start.sh
```

浏览器访问 `http://<设备IP>:8080/`。使用 VLC 打开 `rtsp://<设备IP>:554/live/0`。管理页面显示设备真实状态，默认视频为 720×480、25 目标 FPS、3072 kbps。检测耗时会影响实际帧率，支持范围内的具体视频配置仍需上板验证。

初始化密码至少 10 字节。密码通过终端/stdin 读取，保存为加盐 Argon2id 哈希；没有统一默认密码。配置保存至 `data/config.json`，凭据保存至 `data/auth.json`，文件权限为 0600。建议将整个部署包置于可信用户拥有的目录。

基础示例使用同一个部署包：

```sh
LD_LIBRARY_PATH=./lib ./nopiskl_rtsp_opencv --config ./config/default.json
LD_LIBRARY_PATH=./lib ./nopiskl_rtsp_opencv_capture --config ./config/default.json
```

## 主机联调

```bash
./build.sh --host --jobs 2
cd dist/host
./start.sh --init-password
./start.sh --simulate-motor
```

主机版本明确标记模拟媒体，不调用摄像头、不执行 NPU 推理、不提供 RTSP；截图返回生成的 BMP 测试图。电机默认禁用，只有显式传入 `--simulate-motor` 才启用模拟控制。ARM 版本也可用该参数单独联调电机接口。

```bash
# 从工程根目录运行，测试会自行启动并关闭主机版服务
python3 tests/test_api.py

# 后处理逻辑测试（不依赖摄像头）
cmake --build build/host --target nopiskl_postprocess_test
(cd build/host && ctest --output-on-failure)

# 可选：使用本机 Firefox 验证管理页面，截图写入 docs/previews/
python3 tests/test_browser.py
```

提交和部署归档可通过 `./scripts/package.sh` 生成，输出位于 `dist/archives/`，包含源码包、已生成的 uclibc 部署包及 SHA-256 校验文件。

## 工程结构

```text
apps/SmartIPC/       SmartIPC 应用及其内部模块
    src/            启动组装、命令行、密码初始化
    config/         默认配置
    web/            管理页面静态资源
    modules/
        media/      公共媒体接口、板端与主机模拟后端
        ai/         检测器、模型加载、预处理、推理与 YOLOv5 后处理
        control/    设备协调、配置事务、有界任务队列、截图缓存、电机后端
        config/     SmartIPC 配置加载与校验规则
        web/        登录、会话、鉴权和 API 路由
        common/     文件操作、时间、随机数与系统信息接口
examples/           两个简化的视频采集推流示例
third_party/        平台头文件与库、开源依赖
models/yolov5/      RKNN 模型及 COCO 标签
cmake/              公共编译选项、依赖封装、安装规则、ARM 工具链
docs/               架构、协议、部署及扩展文档
```

项目自有 C++ 实现统一使用 `.cpp`，头文件使用 `.h`。功能模块统一放在 `apps/SmartIPC/modules/`，每个模块按 `include/nopiskl/<模块>/`（公开接口）与 `src/`（实现）组织，并拥有自己的 `CMakeLists.txt`。跨模块使用完整包含路径，例如 `#include "nopiskl/web/auth.h"`。主程序入口是 `apps/SmartIPC/src/main.cpp`，服务组装位于同目录 `application.cpp`；模块关系见 [架构](docs/architecture.md)。

顶层 CMake 负责依赖、应用、示例、测试和安装的组织；`apps/SmartIPC/CMakeLists.txt` 负责内部模块的构建顺序。两个基础示例通过库目标复用媒体模块，公开头文件的包含方式与构建目标名称保持一致。

`Control` 对外提供设备操作，内部由配置事务、任务队列和截图缓存组件承担各自职责；截图下载地址在 HTTP 层生成。板端媒体通过 `YoloV5Detector` 传入 BGR 图像并取得检测结果，模型、预处理缓存和 RKNN 张量由检测器管理。当前仅支持随包的 80 类 COCO YOLOv5 布局，anchors 固定在 `apps/SmartIPC/modules/ai/src/postprocess.cpp`，没有运行时 anchors 配置文件。

管理 HTTP 服务面向可信局域网，当前发行包未编译 TLS；跨不可信网络访问应使用受控 HTTPS 网关或 VPN。HTTP 登录不会自动为现有 RTSP 库添加流媒体鉴权，RTSP 访问控制应由网络策略或后续流服务适配实现。

代码排版及静态检查使用固定规则，开发者可执行 `npm ci`、`npm run format:check` 和 `npm run check:web`。这些工具仅用于开发，不属于板端运行依赖；clang-format、Ruff 的安装与严格编译方式见 [代码规范](docs/coding_standards.md)。

文档入口：[架构](docs/architecture.md)、[构建部署](docs/build_and_deploy.md)、[API](docs/api.md)、[电机协议](docs/motor_protocol.md)、[视频与截图](docs/video_and_snapshot.md)、[扩展指南](docs/extension_guide.md)、[代码规范](docs/coding_standards.md)、[验证记录](docs/validation.md)。
