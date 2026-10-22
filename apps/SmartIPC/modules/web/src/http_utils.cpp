#include "http_utils.h"
#include "civetweb.h"

namespace nopiskl {
namespace http {
void respond_bytes(mg_connection *c, int status, const std::string &mime, const char *bytes,
                   size_t size, const Headers &headers) {
    const char *reason = status < 300 ? "OK" : "Error";
    mg_printf(c,
              "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
              "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
              "X-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\n"
              "Content-Security-Policy: default-src 'self'; img-src 'self' blob:; style-src "
              "'self'; script-src 'self'; frame-ancestors 'none'\r\n",
              status, reason, mime.c_str(), size);
    for (const auto &h : headers) {
        mg_printf(c, "%s: %s\r\n", h.first.c_str(), h.second.c_str());
    }
    mg_printf(c, "\r\n");
    size_t offset = 0;
    while (offset < size) {
        int n = mg_write(c, bytes + offset, size - offset);
        if (n <= 0) {
            break;
        }
        offset += static_cast<size_t>(n);
    }
}
void respond(mg_connection *c, int status, const std::string &mime, const std::string &body,
             const Headers &headers) {
    respond_bytes(c, status, mime, body.data(), body.size(), headers);
}
std::string header(mg_connection *c, const char *name) {
    auto p = mg_get_header(c, name);
    return p ? p : "";
}
std::string session(mg_connection *c) {
    auto cookies = header(c, "Cookie");
    char token[80]{};
    int n = mg_get_cookie(cookies.c_str(), "smartipc_session", token, sizeof(token));
    return n > 0 ? std::string(token) : "";
}
Json body(mg_connection *c) {
    auto info = mg_get_request_info(c);
    if (info->content_length < 0 || info->content_length > 8192 ||
        !header(c, "Transfer-Encoding").empty()) {
        throw Error(413, "invalid_body_size", "Use Content-Length with a JSON body <=8192 bytes");
    }
    auto type = header(c, "Content-Type");
    if (type != "application/json" && type != "application/json; charset=utf-8") {
        throw Error(415, "invalid_content_type", "Content-Type must be application/json");
    }
    std::string text(static_cast<size_t>(info->content_length), '\0');
    size_t offset = 0;
    while (offset < text.size()) {
        int n = mg_read(c, &text[offset], text.size() - offset);
        if (n <= 0) {
            throw Error(400, "incomplete_body", "Incomplete request body");
        }
        offset += static_cast<size_t>(n);
    }
    // 限制深度，防止小体积但深层嵌套的 JSON 耗尽线程栈。
    auto value = Json::parse(text, [](int depth, Json::parse_event_t, Json &) {
        if (depth > 8) {
            throw Error(400, "json_too_deep", "JSON nesting exceeds 8 levels");
        }
        return true;
    });
    if (!value.is_object()) {
        throw Error(400, "invalid_json", "Expected JSON object");
    }
    return value;
}
void fields(const Json &j, const std::vector<std::string> &names) {
    if (j.size() != names.size()) {
        throw Error(400, "invalid_request", "Unexpected or missing fields");
    }
    for (const auto &name : names) {
        if (!j.contains(name) || !j.at(name).is_string()) {
            throw Error(400, "invalid_request", name + " must be a string");
        }
    }
}
bool hex_id(const std::string &id) {
    return id.size() == 16 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
} // namespace http
} // namespace nopiskl
