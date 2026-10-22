#pragma once
#include "nopiskl/media/media.h"

namespace nopiskl {
// 应用启动时选择后端；CMake 决定链接板端或主机模拟实现。
std::unique_ptr<Media> make_media(bool ai, bool opencv_capture = false);
} // namespace nopiskl
