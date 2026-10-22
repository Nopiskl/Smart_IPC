#pragma once
#include "nopiskl/common/types.h"
#include <mutex>

namespace nopiskl {
class Media;
// 查询可来自 HTTP 线程；apply 只在设备工作线程调用，串行执行整个配置事务。
class ConfigService {
public:
    ConfigService(Json config, std::string data_dir);
    Json current() const;
    Json section(const std::string &name) const;
    void validate(const std::string &section, const Json &patch) const;
    Json apply(const std::string &section, const Json &patch, Media &media);

private:
    mutable std::mutex mutex_;
    Json config_;
    std::string file_;
};
} // namespace nopiskl
