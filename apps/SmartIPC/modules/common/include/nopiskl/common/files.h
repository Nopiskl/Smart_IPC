#pragma once
#include <cstddef>
#include <string>

namespace nopiskl {
// 只提供本地文件操作；配置结构和校验由 config 模块负责。
std::string read_file(const std::string &path, size_t limit = 1024 * 1024);
void make_dirs(const std::string &path);
void atomic_write(const std::string &path, const std::string &contents);
} // namespace nopiskl
