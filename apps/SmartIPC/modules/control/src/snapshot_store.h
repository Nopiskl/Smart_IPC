#pragma once
#include "nopiskl/media/media.h"

namespace nopiskl {
// 由 Control 的锁保护；仅持有最新截图，已开始的 HTTP 下载共享不可变图像。
class SnapshotStore {
public:
    Json put(const std::string &id, Image image);
    std::shared_ptr<const Image> get(const std::string &id) const;

private:
    std::string id_;
    std::shared_ptr<const Image> image_;
};
} // namespace nopiskl
