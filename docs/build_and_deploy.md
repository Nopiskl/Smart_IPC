# 构建与部署

## 构建入口

`./build.sh --help` 查看参数。`--target` 接受 `SmartIPC`、`nopiskl_rtsp_opencv`、`nopiskl_rtsp_opencv_capture`、`all`。`--libc` 接受 `uclibc` 和 `glibc`。`--host` 仅构建主机管理/控制模拟程序。

构建缓存分别位于 `build/uclibc`、`build/glibc`、`build/host`，部署包分别位于 `dist/` 对应子目录。目标库与 libc 类型必须一致。

默认 uclibc 工具链读取 `NOPISKL_SDK_PATH`。未设置时尝试工作区相邻的实际 SDK 目录。glibc 读取 `NOPISKL_GLIBC_PREFIX`，要求它是编译器文件名前缀而非目录。

使用 glibc 时，另行准备与板端 ABI 匹配的工具链，在项目根目录执行：

```sh
export NOPISKL_GLIBC_PREFIX=/opt/arm/bin/arm-linux-gnueabihf-
./build.sh --target all --libc glibc
```

依赖已经固定在 `third_party/`：Rockchip MPI/RKNN/RTSP、OpenCV-Mobile、CivetWeb 1.16、Monocypher 4.0.2、nlohmann/json 3.11.3。编译无需联网。

## 安装包

安装包根目录包含可执行程序、`start.sh`、`lib/`、`models/`、`config/`、`web/`、`docs/`。只安装已经构建的二进制。重建覆盖部署包前应保留设备上的 `data/`，其中含配置及账号信息。

程序使用 `$ORIGIN/lib` 运行时搜索路径，启动脚本补充 `LD_LIBRARY_PATH`。跨系统复制仍需保证 ELF interpreter（uclibc/glibc）、内核、媒体驱动与供应商库匹配。不要用 uclibc 程序替换 glibc 固件中的应用。

模型路径默认相对于运行目录。`start.sh` 自动切换到部署包目录；直接运行可执行文件时，使用正确工作目录或在配置中使用绝对路径。

## 启动顺序

1. 确认板端 `/etc/iqfiles` 和传感器配置正确。
2. 停止占用相机及 RTSP 554 端口的原应用，必要时执行固件提供的 `RkLunch-stop.sh`。
3. 将部署包复制至可写分区，首次执行 `./start.sh --init-password`。
4. 执行 `./start.sh`，浏览器访问 8080 端口，检查设备状态。
5. 使用 VLC 等工具拉取 `/live/0`，检查画面颜色、检测框及帧率。

RTSP 默认 554 端口通常需要 root 或相应绑定权限。HTTP 8080 端口可以用 `--port 8081` 临时覆盖，绑定地址和 RTSP 端口通过启动前配置文件设定。

## 部署访问

管理 HTTP 服务面向可信局域网，当前发行包未编译 TLS；跨不可信网络访问应使用受控 HTTPS 网关或 VPN。HTTP 登录不会自动为现有 RTSP 库添加流媒体鉴权，RTSP 访问控制应由网络策略或后续流服务适配实现。

## 配置与凭据

首次启动读取 `config/default.json`；存在 `data/config.json` 时优先读取后者。HTTP 修改将已生效的完整配置写入 `data/config.json`。

管理员用户名为 `admin`，没有统一默认密码，首次运行通过 `./start.sh --init-password` 设置至少 10 字节的密码。密码从终端/stdin 读取，以加盐 Argon2id 哈希保存在 `data/auth.json`；配置与凭据文件权限为 0600，部署目录应由可信用户拥有。

`--data-dir` 选择持久化目录。修改密码使用管理页面或 API。离线恢复时，停止程序，备份并移走该目录的 `auth.json`，再执行 `--init-password`；此操作由有设备文件访问权限的维护者完成。

`--simulate-motor` 只启用模拟命令处理，不操作引脚。物理电机默认返回 501，接入方式见 [电机协议](motor_protocol.md)。

## 基础示例与打包

使用 `./build.sh --target all --libc uclibc --jobs 2` 构建全部程序后，在部署包目录中选择一个基础示例运行；先停止 SmartIPC 或其他占用摄像头的程序：

```sh
LD_LIBRARY_PATH=./lib ./nopiskl_rtsp_opencv --config ./config/default.json
# 或运行 VideoCapture 示例
LD_LIBRARY_PATH=./lib ./nopiskl_rtsp_opencv_capture --config ./config/default.json
```

在项目根目录执行 `./scripts/package.sh` 生成归档，输出位于 `dist/archives/`，包含源码包、已生成的 uclibc 部署包及 SHA-256 校验文件。

## 开发检查

在项目根目录执行以下命令。开发检查需要 Node.js 22.13+ 的 22.x 版本或 24+、Python 3.8+；页面检查工具版本由 `package-lock.json` 固定。这些工具仅用于开发，板端运行无需安装。

```sh
npm ci
python3 -m pip install clang-format==18.1.8 ruff==0.12.0
npm run format:check
npm run check:web
```

排版和静态检查规则由根目录的 `.editorconfig`、`.clang-format`、`.prettierrc.json`、`ruff.toml` 和 `eslint.config.mjs` 维护。需要重新排版时执行 `npm run format`；已有工具可通过 `NOPISKL_CLANG_FORMAT`、`NOPISKL_PRETTIER`、`NOPISKL_RUFF` 指定路径。

主机严格编译及行为测试：

```sh
cmake -S . -B build/host -DNOPISKL_HOST_SIMULATOR=ON -DNOPISKL_WARNINGS_AS_ERRORS=ON
cmake --build build/host --parallel 2
(cd build/host && ctest --output-on-failure)
python3 tests/test_cli.py
python3 tests/test_api.py
```

完整构建同时检查公开头文件能否独立包含。CTest 覆盖 YOLOv5 后处理和控制层后端注入，CLI 与 HTTP 测试覆盖参数校验、鉴权、配置、任务及截图等行为。安装 Firefox 后，可执行 `python3 tests/test_browser.py` 检查页面操作和错误恢复。

## 验证范围

以下为 2026-10-22 已有检查结果的摘要，本次文档整理未重新执行构建或设备测试：

| 范围 | 已有结果 |
| --- | --- |
| 主机与 ARM uclibc | 严格告警构建通过；ARM 包含 SmartIPC 和两个基础示例 |
| 主机行为与接口 | 2 项 CTest、7 项 CLI 测试、12 项 HTTP 测试通过 |
| 公开头文件 | 13 个头文件独立编译通过 |

真实摄像头、ISP、NPU、JPEG、RTSP 和连续运行内存仍需在目标板验证；glibc 尚未完成编译验证。物理电机驱动尚未实现，模拟命令成功不能证明实际电机动作成功。

板端验收应覆盖画面颜色与检测框、JPEG/BMP 截图、视频参数修改后的 RTSP 重连、配置失败恢复、重启后的配置与账号保留，以及连续运行内存和退出后资源释放。具体数据流见 [架构](architecture.md)，请求字段和任务结果见 [API](api.md)。
