#pragma once
#include "nopiskl/common/types.h"
#include <map>
#include <mutex>

namespace nopiskl {
class Auth {
public:
    explicit Auth(const std::string &data_dir);
    static void initialize(const std::string &data_dir, const std::string &password);
    Json login(const std::string &username, const std::string &password);
    Json authorize(const std::string &token, const std::string &csrf, bool mutation);
    void logout(const std::string &token);
    void change_password(const std::string &current, const std::string &next);

private:
    std::mutex mutex_;
    std::string file_;
    Json credentials_;
    struct Session {
        std::string csrf;
        uint64_t expires;
    };
    std::map<std::string, Session> sessions_;
    uint64_t retry_at_ = 0;
    unsigned failures_ = 0;
};
} // namespace nopiskl
