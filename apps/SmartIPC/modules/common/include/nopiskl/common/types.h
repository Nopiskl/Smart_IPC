#pragma once

#include "json.hpp"
#include <stdexcept>
#include <string>

namespace nopiskl {
using Json = nlohmann::json;

// 状态码同时用于 HTTP 和将来接入的串口/TCP 适配层。
struct Error : std::runtime_error {
    int status;
    std::string code;
    Error(int status_, const std::string &code_, const std::string &message)
        : std::runtime_error(message), status(status_), code(code_) {
    }
};

} // namespace nopiskl
