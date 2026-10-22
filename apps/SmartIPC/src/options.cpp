#include "options.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace nopiskl {
namespace smartipc {
namespace {
int parse_port(const std::string &value) {
    const auto invalid = []() {
        return std::runtime_error("HTTP port must be a decimal integer in 1024..65535");
    };
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        throw invalid();
    }
    // 限定数字长度，避免溢出以及 std::stoi 接受 8080suffix 之类的输入。
    if (value.size() > 5) {
        throw invalid();
    }
    const int port = std::stoi(value);
    if (port < 1024 || port > 65535) {
        throw invalid();
    }
    return port;
}

} // namespace

Options parse_options(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto value = [&]() -> std::string {
            if (++i == argc) {
                throw std::runtime_error("Missing value for " + argument);
            }
            return argv[i];
        };
        if (argument == "--config") {
            options.defaults = value();
        } else if (argument == "--data-dir") {
            options.data = value();
        } else if (argument == "--web-root") {
            options.web = value();
        } else if (argument == "--port") {
            options.port = parse_port(value());
        } else if (argument == "--init-password") {
            options.initialize = true;
        } else if (argument == "--simulate-motor") {
            options.simulate_motor = true;
        } else if (argument == "--help") {
            options.help = true;
        } else {
            throw std::runtime_error("Unknown option: " + argument);
        }
    }
    return options;
}

void print_help() {
    std::cout
        << "nopiskl SmartIPC 1.0.0\n"
        << "  --config PATH       Default configuration (saved data/config.json takes precedence)\n"
        << "  --data-dir PATH     Private writable state directory\n"
        << "  --web-root PATH     Static management assets\n"
        << "  --port NUMBER       Temporary HTTP listen port override (1024..65535)\n"
        << "  --init-password     Read initial admin password from terminal/stdin, then exit\n"
        << "  --simulate-motor    Enable explicitly simulated motor (no GPIO/UART activity)\n";
}

} // namespace smartipc
} // namespace nopiskl
