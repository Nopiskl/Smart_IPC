#include "nopiskl/web/server.h"
#include "nopiskl/web/auth.h"
#include "nopiskl/control/control.h"
#include "nopiskl/common/files.h"
#include "nopiskl/common/system.h"
#include "http_utils.h"
#include "civetweb.h"
#include <atomic>
#include <iostream>
#include <map>

namespace nopiskl {
Server::Server(Control &c, Auth &a, std::string root)
    : control_(c), auth_(a), web_root_(std::move(root)) {
}
Server::~Server() {
    stop();
}
void Server::start(const Json &http) {
    for (const auto *name : {"index.html", "app.js", "style.css"}) {
        read_file(web_root_ + "/" + name);
    }
    std::string listen =
        http.at("bind").get<std::string>() + ":" + std::to_string(http.at("port").get<int>());
    const char *options[] = {"listening_ports",
                             listen.c_str(),
                             "num_threads",
                             "2",
                             "enable_keep_alive",
                             "no",
                             "request_timeout_ms",
                             "3000",
                             "max_request_size",
                             "8192",
                             nullptr};
    mg_callbacks callbacks{};
    callbacks.begin_request = &Server::dispatch;
    context_ = mg_start(&callbacks, this, options);
    if (!context_) {
        throw std::runtime_error("Cannot start HTTP service at " + listen);
    }
    std::cout << "[nopiskl] SmartIPC management: http://" << listen << "/\n";
}
void Server::stop() {
    if (context_) {
        mg_stop(context_);
    }
    context_ = nullptr;
}
int Server::dispatch(mg_connection *c) {
    auto *server = static_cast<Server *>(mg_get_request_info(c)->user_data);
    return server->handle(c);
}
int Server::handle(mg_connection *c) {
    static std::atomic<uint64_t> sequence{0};
    const std::string request_id = std::to_string(++sequence);
    auto reply = [&](int status, const Json &data, const http::Headers &headers = http::Headers{}) {
        http::respond(
            c, status, "application/json; charset=utf-8",
            Json{{"code", "ok"}, {"message", "OK"}, {"request_id", request_id}, {"data", data}}
                .dump(),
            headers);
    };
    try {
        const auto *info = mg_get_request_info(c);
        std::string path = info->local_uri ? info->local_uri : "";
        std::string method = info->request_method;
        const std::map<std::string, std::pair<std::string, std::string>> assets{
            {"/", {"index.html", "text/html; charset=utf-8"}},
            {"/app.js", {"app.js", "application/javascript; charset=utf-8"}},
            {"/style.css", {"style.css", "text/css; charset=utf-8"}}};
        if (method == "GET" && assets.count(path)) {
            const auto &asset = assets.at(path);
            http::respond(c, 200, asset.second, read_file(web_root_ + "/" + asset.first));
            return 200;
        }
        if (path == "/api/v1/auth/login" && method == "POST") {
            auto input = http::body(c);
            http::fields(input, {"username", "password"});
            auto login = auth_.login(input.at("username"), input.at("password"));
            std::string cookie = "smartipc_session=" + login.at("token").get<std::string>() +
                                 "; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800";
            login.erase("token");
            reply(200, login, {{"Set-Cookie", cookie}});
            return 200;
        }
        if (path.compare(0, 8, "/api/v1/") != 0) {
            throw Error(404, "not_found", "Resource not found");
        }
        auto token = http::session(c);
        auto user = auth_.authorize(token, http::header(c, "X-CSRF-Token"), method != "GET");
        if (path == "/api/v1/auth/session" && method == "GET") {
            reply(200, user);
        } else if (path == "/api/v1/auth/logout" && method == "POST") {
            auth_.logout(token);
            reply(200, Json::object(),
                  {{"Set-Cookie",
                    "smartipc_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"}});
        } else if (path == "/api/v1/auth/password" && method == "PUT") {
            auto input = http::body(c);
            http::fields(input, {"current_password", "new_password"});
            auth_.change_password(input.at("current_password"), input.at("new_password"));
            reply(200, {{"login_required", true}},
                  {{"Set-Cookie",
                    "smartipc_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"}});
        } else if (path == "/api/v1/device/info" && method == "GET") {
            reply(200, device_info());
        } else if (path == "/api/v1/device/status" && method == "GET") {
            reply(200, control_.status());
        } else if (path == "/api/v1/capabilities" && method == "GET") {
            auto status = control_.status();
            bool simulated = status.at("media").value("simulated", false);
            reply(200, {{"api_version", "v1"},
                        {"simulated_media", simulated},
                        {"codecs", simulated ? Json::array() : Json::array({"h264"})},
                        {"snapshot_formats",
                         simulated ? Json::array({"bmp"}) : Json::array({"jpeg", "bmp"})},
                        {"motor", status.at("motor")},
                        {"browser_live_video", false},
                        {"video_constraints",
                         {{"width_min", 320},
                          {"width_max", 1920},
                          {"width_alignment", 16},
                          {"height_min", 240},
                          {"height_max", 1080},
                          {"height_alignment", 2},
                          {"fps_max", 30}}}});
        } else if (path == "/api/v1/motor/status" && method == "GET") {
            reply(200, control_.status().at("motor"));
        } else if (path == "/api/v1/motor/commands" && method == "POST") {
            reply(202, control_.submit_motor(http::body(c)));
        } else if (path == "/api/v1/snapshots" && method == "POST") {
            reply(202, control_.submit_snapshot());
        } else if (path.compare(0, 18, "/api/v1/snapshots/") == 0 && method == "GET") {
            auto id = path.substr(18);
            if (!http::hex_id(id)) {
                throw Error(404, "snapshot_not_found", "Snapshot not found");
            }
            auto image = control_.image(id);
            http::respond_bytes(c, 200, image->mime,
                                reinterpret_cast<const char *>(image->bytes.data()),
                                image->bytes.size());
        } else if (path.compare(0, 14, "/api/v1/tasks/") == 0 && method == "GET") {
            auto id = path.substr(14);
            if (!http::hex_id(id)) {
                throw Error(404, "task_not_found", "Task not found");
            }
            auto task = control_.task(id);
            if (task.at("type") == "snapshot" && task.at("state") == "succeeded") {
                // 下载地址属于 HTTP 表示，控制层只返回截图标识和元数据。
                task["result"]["url"] =
                    "/api/v1/snapshots/" + task.at("result").at("snapshot_id").get<std::string>();
            }
            reply(200, task);
        } else {
            std::string section;
            if (path == "/api/v1/video/config") {
                section = "video";
            } else if (path == "/api/v1/ai/config") {
                section = "ai";
            } else if (path == "/api/v1/snapshot/config") {
                section = "snapshot";
            }
            if (section.empty()) {
                throw Error(404, "not_found", "API route not found");
            }
            if (method == "GET") {
                reply(200, control_.config(section));
            } else if (method == "PUT") {
                reply(202, control_.submit_config(section, http::body(c)));
            } else {
                throw Error(405, "method_not_allowed", "Use GET or PUT");
            }
        }
    } catch (const std::exception &e) {
        const auto *error = dynamic_cast<const Error *>(&e);
        const auto *json_error = dynamic_cast<const Json::exception *>(&e);
        int status = error ? error->status : json_error ? 400 : 500;
        std::string code = error ? error->code : json_error ? "invalid_json" : "internal_error";
        http::respond(c, status, "application/json; charset=utf-8",
                      Json{{"code", code},
                           {"message", e.what()},
                           {"request_id", request_id},
                           {"data", nullptr}}
                          .dump());
        return status;
    }
    return 200;
}
} // namespace nopiskl
