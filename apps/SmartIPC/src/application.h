#pragma once
#include <csignal>

namespace nopiskl {
namespace smartipc {
// 应用组装点：选择后端，管理服务启动、停止及对象销毁顺序。
int run_application(int argc, char **argv, const volatile std::sig_atomic_t &stop_requested);
} // namespace smartipc
} // namespace nopiskl
