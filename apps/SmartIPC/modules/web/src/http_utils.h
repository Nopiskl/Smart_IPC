#pragma once
#include "nopiskl/common/types.h"
#include <map>
#include <vector>

struct mg_connection;
namespace nopiskl {
namespace http {
using Headers = std::map<std::string, std::string>;
// 仅供 web 实现使用；CivetWeb 类型和协议解析不会传播到业务层。
void respond_bytes(mg_connection *connection, int status, const std::string &mime,
                   const char *bytes, size_t size, const Headers &headers = {});
void respond(mg_connection *connection, int status, const std::string &mime,
             const std::string &body, const Headers &headers = {});
std::string header(mg_connection *connection, const char *name);
std::string session(mg_connection *connection);
Json body(mg_connection *connection);
void fields(const Json &value, const std::vector<std::string> &names);
bool hex_id(const std::string &id);
} // namespace http
} // namespace nopiskl
