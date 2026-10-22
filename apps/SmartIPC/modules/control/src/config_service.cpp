#include "config_service.h"
#include "nopiskl/config/config.h"
#include "nopiskl/common/files.h"
#include "nopiskl/media/media.h"
#include <iostream>

namespace nopiskl {
ConfigService::ConfigService(Json config, std::string data_dir)
    : config_(std::move(config)), file_(std::move(data_dir) + "/config.json") {
}
Json ConfigService::current() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}
Json ConfigService::section(const std::string &name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.at(name);
}
void ConfigService::validate(const std::string &section, const Json &patch) const {
    validate_patch(section, patch);
    auto candidate = current();
    candidate[section].merge_patch(patch);
    validate_config(candidate);
}
Json ConfigService::apply(const std::string &section, const Json &patch, Media &media) {
    // 执行时重新合并最新生效配置，避免排队期间其他 patch 被覆盖。
    const auto before = current();
    auto candidate = before;
    candidate[section].merge_patch(patch);
    validate_config(candidate);
    try {
        media.configure(candidate);
        atomic_write(file_, candidate.dump(2) + "\n");
    } catch (...) {
        try {
            media.configure(before);
        } catch (const std::exception &error) {
            std::cerr << "[nopiskl] rollback failed: " << error.what() << '\n';
        }
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = candidate;
    }
    return {{"applied", candidate.at(section)}, {"rtsp_reconnect_required", section == "video"}};
}
} // namespace nopiskl
