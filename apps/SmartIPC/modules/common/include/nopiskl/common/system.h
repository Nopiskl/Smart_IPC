#pragma once
#include "nopiskl/common/types.h"
#include <cstdint>

namespace nopiskl {
// 使用单调时钟计算超时；设备信息独立于媒体和 HTTP 服务。
uint64_t now_ms();
std::string random_hex(size_t bytes);
Json device_info();
Json memory_info();
} // namespace nopiskl
