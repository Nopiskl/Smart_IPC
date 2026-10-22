# 构建与部署

## 构建入口

`./build.sh --help` 查看参数。`--target` 接受 `SmartIPC`、`nopiskl_rtsp_opencv`、`nopiskl_rtsp_opencv_capture`、`all`。`--libc` 接受 `uclibc` 和 `glibc`。`--host` 仅构建主机管理/控制模拟程序。

构建缓存分别位于 `build/uclibc`、`build/glibc`、`build/host`，部署包分别位于 `dist/` 对应子目录。目标库与 libc 类型必须一致。

默认 uclibc 工具链读取 `NOPISKL_SDK_PATH`。未设置时尝试工作区相邻的实际 SDK 目录。glibc 读取 `NOPISKL_GLIBC_PREFIX`，要求它是编译器文件名前缀而非目录。

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

## 配置与凭据

首次启动读取 `config/default.json`；存在 `data/config.json` 时优先读取后者。HTTP 修改将已生效的完整配置写入 `data/config.json`。

`--data-dir` 选择持久化目录。修改密码使用管理页面或 API。离线恢复时，停止程序，备份并移走该目录的 `auth.json`，再执行 `--init-password`；此操作由有设备文件访问权限的维护者完成。

`--simulate-motor` 只启用模拟命令处理，不操作引脚。物理电机默认返回 501，接入方式见电机文档。
