#include "application.h"
#include <csignal>
#include <exception>
#include <iostream>

namespace {
volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int) {
    // 信号处理函数只设置标志，资源释放在正常控制流完成。
    stop_requested = 1;
}
} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    std::signal(SIGPIPE, SIG_IGN);
    try {
        return nopiskl::smartipc::run_application(argc, argv, stop_requested);
    } catch (const std::exception &error) {
        std::cerr << "[nopiskl] " << error.what() << '\n';
        return 1;
    }
}
