# 代码排版与工程检查规范

本规范适用于本项目维护的 `apps/`（含 `apps/SmartIPC/modules/`）、`examples/`、`tests/` 和检查脚本。供应商源码、预编译库、模型及生成的部署文件不参与源码格式化，第三方版权说明保持原样。

## 命名与模块边界

项目自有 C++ 实现文件一律使用 `.cpp`，头文件使用 `.h`。检查脚本发现旧 C++ 实现扩展名时直接失败，不会静默漏检。模块公开头文件放在 `include/nopiskl/<模块>/`，实现和内部辅助头文件放在 `src/`；跨模块包含使用 `nopiskl/<模块>/<文件>.h`。

每个模块拥有自己的 `CMakeLists.txt`，仅公开调用者实际需要的包含路径和依赖；由 `apps/SmartIPC/CMakeLists.txt` 统一组装。应用初始化、参数解析和密码输入分别放在 SmartIPC 的 `application`、`options`、`admin_setup`；`main.cpp` 保持为信号和顶层异常入口。

## 排版规则

| 文件 | 规则与检查工具 |
| --- | --- |
| C/C++ | C++14，4 空格缩进，目标行宽 100；条件与循环使用大括号；不将多个独立语句压在同一行；clang-format 18.1.8 |
| JavaScript | 2 空格缩进，目标行宽 100，单引号和分号；展开条件、循环、异步错误处理；Prettier 3.6.2 |
| HTML/CSS | 2 空格缩进；按标签层级及样式声明分行，保留 HTML 文本所需空白；Prettier 3.6.2 |
| Python | 4 空格缩进，目标行宽 100，兼容 Python 3.8；Ruff 0.12.0 |
| 通用 | UTF-8、LF、文件末尾换行、清理行尾空白；规则见 `.editorconfig` |

简单表达式和短 lambda 可以保持紧凑；行宽是格式化目标，无法安全拆分的字符串或注释可超过该目标。源代码使用有意义的名称，并在资源所有权、线程边界、错误处理及外部扩展位置补充注释。

JavaScript 使用 ESLint 10.13.0 检查未定义变量、未使用变量、不可达语句、重复对象字段、严格比较和控制流大括号等。规则位于 `eslint.config.mjs`。

工具规则参考：[clang-format 官方选项](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)、[Prettier 官方选项](https://prettier.io/docs/options)。项目固定工具版本，以避免不同开发环境生成不一致的排版。

## 开发环境检查

Node.js 22.13+ 的 22.x 版本或 24+、Python、clang-format 与 Ruff 仅用于开发检查。板端运行 SmartIPC 不需要这些开发工具。

```sh
# 只安装格式化及静态检查依赖，不用于构建板端页面
npm ci
python3 -m pip install clang-format==18.1.8 ruff==0.12.0

# 不修改文件：检查所有项目自有 C/C++、页面与 Python 文件
npm run format:check

# JavaScript 语法与静态检查
npm run check:web

# 需要重新排版时执行
npm run format
```

`scripts/check_style.py` 也可直接执行；支持用 `NOPISKL_CLANG_FORMAT`、`NOPISKL_PRETTIER`、`NOPISKL_RUFF` 指向已有工具。缺少工具时明确失败，不将“没有执行检查”记作通过。

## 编译和行为验证

所有项目自有 C++ 目标统一启用 `-Wall -Wextra -Wpedantic`，可通过 `NOPISKL_WARNINGS_AS_ERRORS=ON` 将警告作为错误处理：

供应商头文件以 CMake `SYSTEM` 包含路径引入。RKAIQ 头文件存在匿名结构体等历史扩展，这些供应商诊断与项目自有代码分开处理；项目自身的警告检查继续开启，没有将供应商头文件改写为另一套 ABI。

```sh
cmake -S . -B build/host -DNOPISKL_HOST_SIMULATOR=ON -DNOPISKL_WARNINGS_AS_ERRORS=ON
cmake --build build/host --parallel 2
(cd build/host && ctest --output-on-failure)
python3 tests/test_cli.py
python3 tests/test_api.py
python3 tests/test_browser.py
```

主机完整构建同时检查各公开头文件能否独立包含。CTest 包含 YOLOv5 后处理和可替换控制后端两类测试，后者验证异常回滚、配置保存、单线程访问及退出停止。

命令行检查覆盖端口格式、参数缺失、密码长度、CRLF 输入和信号中断后的终端恢复。HTTP 测试覆盖鉴权、参数校验、任务、配置及截图等。Firefox 测试覆盖正常操作和网络失败路径，并捕获未处理的脚本异常。

## 检查范围与结论边界

本项目的工程检查依据为上述排版规则、C++14 构建约束、文档中的 HTTP API 契约和现有功能要求。通过这些检查不等同于 MISRA/AUTOSAR 等专用标准认证，也不代表已经完成安全认证、供应商授权审计或实际板卡验收。

真实摄像头、ISP、NPU、JPEG、RTSP 和内存稳定性仍须按 `validation.md` 在目标板验证；主机模拟及合成张量测试分别记录结果，不与实际硬件结果混合。
