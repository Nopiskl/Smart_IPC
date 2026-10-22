#include "nopiskl/config/config.h"
#include "nopiskl/common/files.h"
#include <arpa/inet.h>
#include <set>
#include <unistd.h>

namespace nopiskl {
static void keys(const Json &j, const std::set<std::string> &allowed) {
    if (!j.is_object() || j.empty()) {
        throw Error(400, "invalid_config", "Expected non-empty JSON object");
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!allowed.count(it.key())) {
            throw Error(400, "unknown_field", "Unknown field: " + it.key());
        }
    }
}
static void integer(const Json &j, const char *key, int min, int max) {
    const auto &v = j.at(key);
    if (!v.is_number_integer() || v.get<int64_t>() < min || v.get<int64_t>() > max) {
        throw Error(400, "invalid_config", std::string(key) + " out of range");
    }
}
void validate_patch(const std::string &s, const Json &p) {
    if (s == "video") {
        keys(p, {"codec", "width", "height", "fps", "bitrate_kbps", "gop"});
    } else if (s == "ai") {
        keys(p, {"enabled", "overlay", "confidence", "nms"});
    } else if (s == "snapshot") {
        keys(p, {"format", "quality", "overlay"});
    } else {
        throw Error(404, "not_found", "Unknown configuration section");
    }
}
void validate_config(const Json &c) {
    keys(c, {"device_name", "http", "video", "ai", "snapshot"});
    if (!c.at("device_name").is_string() || c.at("device_name").get<std::string>().empty() ||
        c.at("device_name").get<std::string>().size() > 64) {
        throw Error(400, "invalid_config", "Invalid device name");
    }
    const auto &h = c.at("http");
    keys(h, {"bind", "port"});
    integer(h, "port", 1024, 65535);
    struct in_addr bind_address {};
    if (!h.at("bind").is_string() ||
        inet_pton(AF_INET, h.at("bind").get<std::string>().c_str(), &bind_address) != 1) {
        throw Error(400, "invalid_config", "http.bind must be an IPv4 address");
    }
    const auto &v = c.at("video");
    keys(v, {"codec", "width", "height", "fps", "bitrate_kbps", "gop", "rtsp_port", "rtsp_path"});
    if (v.at("codec") != "h264") {
        throw Error(400, "unsupported_codec",
                    "This release exposes verified H.264 configuration only");
    }
    integer(v, "width", 320, 1920);
    integer(v, "height", 240, 1080);
    if (v.at("width").get<int>() % 16 || v.at("height").get<int>() % 2) {
        throw Error(400, "invalid_config", "Width must align to 16 pixels and height to 2");
    }
    integer(v, "fps", 1, 30);
    integer(v, "bitrate_kbps", 128, 16384);
    integer(v, "gop", 1, 300);
    integer(v, "rtsp_port", 1, 65535);
    if (v.at("rtsp_path") != "/live/0") {
        throw Error(400, "invalid_config", "rtsp_path must be /live/0");
    }
    const auto &a = c.at("ai");
    keys(a, {"enabled", "overlay", "confidence", "nms", "model_path", "labels_path"});
    for (const char *key : {"enabled", "overlay"}) {
        if (!a.at(key).is_boolean()) {
            throw Error(400, "invalid_config", std::string(key) + " must be boolean");
        }
    }
    for (const char *key : {"confidence", "nms"}) {
        if (!a.at(key).is_number() || a.at(key).get<double>() <= 0 ||
            a.at(key).get<double>() >= 1) {
            throw Error(400, "invalid_config", std::string(key) + " must be between 0 and 1");
        }
    }
    for (const char *key : {"model_path", "labels_path"}) {
        if (!a.at(key).is_string() || a.at(key).get<std::string>().empty()) {
            throw Error(400, "invalid_config", std::string(key) + " must be a path");
        }
    }
    const auto &s = c.at("snapshot");
    keys(s, {"format", "quality", "overlay"});
    integer(s, "quality", 1, 100);
    if (s.at("format") != "jpeg" && s.at("format") != "bmp") {
        throw Error(400, "invalid_config", "Snapshot format must be jpeg or bmp");
    }
    if (!s.at("overlay").is_boolean()) {
        throw Error(400, "invalid_config", "overlay must be boolean");
    }
}
Json load_config(const std::string &defaults, const std::string &data_dir) {
    auto c = Json::parse(read_file(defaults, 32768));
    const auto saved = data_dir + "/config.json";
    if (access(saved.c_str(), F_OK) == 0) {
        c = Json::parse(read_file(saved, 32768));
    }
    validate_config(c);
    return c;
}
} // namespace nopiskl
