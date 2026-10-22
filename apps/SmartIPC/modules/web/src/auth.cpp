#include "nopiskl/web/auth.h"
#include "nopiskl/common/files.h"
#include "nopiskl/common/system.h"
#include "monocypher.h"
#include <algorithm>
#include <array>
#include <vector>
#include <unistd.h>

namespace nopiskl {
static std::string derive(const std::string &password, const std::string &salt) {
    // Argon2id: 8 MiB 临时内存、3 轮、单通道。Auth 的锁使多个登录不会叠加内存。
    std::vector<uint8_t> work(8192 * 1024);
    std::array<uint8_t, 32> hash{};
    crypto_argon2_config cfg{CRYPTO_ARGON2_ID, 8192, 3, 1};
    crypto_argon2_inputs input{reinterpret_cast<const uint8_t *>(password.data()),
                               reinterpret_cast<const uint8_t *>(salt.data()),
                               static_cast<uint32_t>(password.size()),
                               static_cast<uint32_t>(salt.size())};
    crypto_argon2(hash.data(), hash.size(), work.data(), cfg, input, crypto_argon2_no_extras);
    std::string hex;
    const char *digits = "0123456789abcdef";
    for (auto byte : hash) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    crypto_wipe(hash.data(), hash.size());
    crypto_wipe(work.data(), work.size());
    return hex;
}
static bool equal_secret(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) {
        return false;
    }
    volatile unsigned diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}
static Json credentials(const std::string &password) {
    if (password.size() < 10 || password.size() > 128) {
        throw Error(400, "invalid_password", "Password must contain 10..128 bytes");
    }
    auto salt = random_hex(16);
    return {{"username", "admin"},
            {"kdf", "argon2id-8192-3-1"},
            {"salt", salt},
            {"hash", derive(password, salt)}};
}
void Auth::initialize(const std::string &dir, const std::string &password) {
    make_dirs(dir);
    if (access((dir + "/auth.json").c_str(), F_OK) == 0) {
        throw std::runtime_error(
            "Administrator already initialized; use the password API to change it");
    }
    atomic_write(dir + "/auth.json", credentials(password).dump(2) + "\n");
}
Auth::Auth(const std::string &dir) : file_(dir + "/auth.json") {
    if (access(file_.c_str(), F_OK)) {
        throw std::runtime_error("Initialize administrator first: ./SmartIPC --init-password");
    }
    credentials_ = Json::parse(read_file(file_, 4096));
    if (credentials_.value("kdf", "") != "argon2id-8192-3-1" ||
        credentials_.at("salt").get<std::string>().size() != 32 ||
        credentials_.at("hash").get<std::string>().size() != 64) {
        throw std::runtime_error("Invalid credential file");
    }
}
Json Auth::login(const std::string &username, const std::string &password) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (now_ms() < retry_at_) {
        throw Error(429, "login_throttled", "Wait before retrying login");
    }
    if (password.size() > 128) {
        throw Error(400, "invalid_password", "Password too long");
    }
    auto hash = derive(password, credentials_.at("salt"));
    if (!equal_secret(hash, credentials_.at("hash")) || username != "admin") {
        retry_at_ = now_ms() + std::min(30000u, 1000u * ++failures_);
        throw Error(401, "invalid_credentials", "Invalid username or password");
    }
    failures_ = 0;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.expires <= now_ms()) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
    if (sessions_.size() >= 4) {
        auto oldest =
            std::min_element(sessions_.begin(), sessions_.end(), [](const auto &a, const auto &b) {
                return a.second.expires < b.second.expires;
            });
        sessions_.erase(oldest);
    }
    auto token = random_hex(32), csrf = random_hex(16);
    sessions_[token] = {csrf, now_ms() + 1800000};
    return {{"token", token}, {"csrf_token", csrf}, {"expires_in", 1800}, {"username", "admin"}};
}
Json Auth::authorize(const std::string &token, const std::string &csrf, bool mutation) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end() || it->second.expires <= now_ms()) {
        if (it != sessions_.end()) {
            sessions_.erase(it);
        }
        throw Error(401, "unauthorized", "Login required");
    }
    if (mutation && !equal_secret(csrf, it->second.csrf)) {
        throw Error(403, "csrf_invalid", "Invalid CSRF token");
    }
    return {{"username", "admin"}, {"csrf_token", it->second.csrf}};
}
void Auth::logout(const std::string &token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}
void Auth::change_password(const std::string &current, const std::string &next) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current.size() > 128 ||
        !equal_secret(derive(current, credentials_.at("salt")), credentials_.at("hash"))) {
        throw Error(401, "invalid_credentials", "Current password incorrect");
    }
    auto value = credentials(next);
    atomic_write(file_, value.dump(2) + "\n");
    credentials_ = value;
    sessions_.clear();
}
} // namespace nopiskl
