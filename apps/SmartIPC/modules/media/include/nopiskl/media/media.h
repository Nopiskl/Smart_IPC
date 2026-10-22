#pragma once
#include "nopiskl/common/types.h"
#include <memory>
#include <vector>

namespace nopiskl {
struct Image {
    std::string mime;
    std::vector<unsigned char> bytes;
};
// 只有控制工作线程访问 Media。HTTP 线程通过 Control 提交任务，避免跨线程重建 VI/VENC。
class Media {
public:
    virtual ~Media() = default;
    virtual void configure(const Json &config) = 0;
    virtual void step() = 0;
    virtual Image snapshot(const Json &options) = 0;
    virtual Json status() const = 0;
};
} // namespace nopiskl
