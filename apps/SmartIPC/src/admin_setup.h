#pragma once
#include <csignal>
#include <string>

namespace nopiskl {
namespace smartipc {
// 终端回显与密码缓冲的生命周期均由初始化过程管理。
void initialize_administrator(const std::string &data_dir,
                              const volatile std::sig_atomic_t &stop_requested);
} // namespace smartipc
} // namespace nopiskl
