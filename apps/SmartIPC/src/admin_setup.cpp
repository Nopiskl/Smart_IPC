#include "admin_setup.h"
#include "nopiskl/web/auth.h"
#include "monocypher.h"
#include <cerrno>
#include <iostream>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace nopiskl {
namespace smartipc {
namespace {
class TerminalEchoGuard {
public:
    TerminalEchoGuard() {
        if (!isatty(STDIN_FILENO)) {
            return;
        }
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) {
            throw std::runtime_error("Cannot read terminal settings");
        }
        auto hidden = saved_;
        hidden.c_lflag &= ~ECHO;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) != 0) {
            throw std::runtime_error("Cannot disable terminal echo");
        }
        changed_ = true;
    }
    ~TerminalEchoGuard() {
        if (changed_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
            std::cout << '\n';
        }
    }
    TerminalEchoGuard(const TerminalEchoGuard &) = delete;
    TerminalEchoGuard &operator=(const TerminalEchoGuard &) = delete;

private:
    termios saved_{};
    bool changed_ = false;
};

class PasswordInput {
public:
    PasswordInput() {
        value_.reserve(130);
    }

    PasswordInput(const PasswordInput &) = delete;
    PasswordInput &operator=(const PasswordInput &) = delete;

    void read(const volatile std::sig_atomic_t &stop_requested) {
        TerminalEchoGuard terminal;
        if (isatty(STDIN_FILENO)) {
            std::cout << "Initial admin password (10..128 bytes): " << std::flush;
        }
        // 有限轮询使 SIGINT/SIGTERM 能退出输入并恢复终端回显。
        while (!stop_requested) {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const int ready = poll(&input, 1, 100);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("Cannot poll password input");
            }
            if (ready == 0) {
                continue;
            }
            char byte = 0;
            const ssize_t count = ::read(STDIN_FILENO, &byte, 1);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("Cannot read password input");
            }
            if (count == 0 || byte == '\n') {
                if (!value_.empty() && value_.back() == '\r') {
                    value_.pop_back();
                }
                return;
            }
            value_ += byte;
            if (value_.size() > 128 && !(value_.size() == 129 && byte == '\r')) {
                throw std::runtime_error("Password must contain 10..128 bytes");
            }
        }
        throw std::runtime_error("Password initialization interrupted");
    }
    ~PasswordInput() {
        if (!value_.empty()) {
            crypto_wipe(&value_[0], value_.size());
        }
    }
    const std::string &value() const {
        return value_;
    }

private:
    std::string value_;
};
} // namespace

void initialize_administrator(const std::string &data_dir,
                              const volatile std::sig_atomic_t &stop_requested) {
    PasswordInput password;
    password.read(stop_requested);
    Auth::initialize(data_dir, password.value());
    std::cout << "Administrator initialized. Username: admin\n";
}
} // namespace smartipc
} // namespace nopiskl
