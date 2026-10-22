#include "nopiskl/common/files.h"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace nopiskl {
std::string read_file(const std::string &path, size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open " + path);
    }
    std::string out;
    char buf[4096];
    while (file.read(buf, sizeof(buf)) || file.gcount()) {
        out.append(buf, static_cast<size_t>(file.gcount()));
        if (out.size() > limit) {
            throw std::runtime_error("File too large: " + path);
        }
    }
    if (file.bad()) {
        throw std::runtime_error("Read failed: " + path);
    }
    return out;
}
void make_dirs(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("Empty directory");
    }
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') {
            continue;
        }
        const auto part = path.substr(0, i);
        if (mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) {
            throw std::runtime_error("mkdir " + part + ": " + std::strerror(errno));
        }
        struct stat st {};
        if (stat(part.c_str(), &st) || !S_ISDIR(st.st_mode)) {
            throw std::runtime_error("Not a directory: " + part);
        }
    }
}
void atomic_write(const std::string &path, const std::string &contents) {
    // 临时文件与目标位于同一目录；先 fsync 再 rename，避免断电留下半份 JSON。
    static std::atomic<unsigned> seq{0};
    std::string tmp = path + ".tmp." + std::to_string(getpid()) + "." + std::to_string(seq++);
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::runtime_error("Cannot create " + tmp);
    }
    bool ok = true;
    size_t written = 0;
    while (written < contents.size()) {
        ssize_t n = write(fd, contents.data() + written, contents.size() - written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            ok = false;
            break;
        }
        written += static_cast<size_t>(n);
    }
    if (ok && fsync(fd)) {
        ok = false;
    }
    if (close(fd)) {
        ok = false;
    }
    if (!ok || rename(tmp.c_str(), path.c_str())) {
        unlink(tmp.c_str());
        throw std::runtime_error("Cannot save " + path);
    }
    auto parent = path.substr(0, path.find_last_of('/'));
    int dir = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        fsync(dir);
        close(dir);
    }
}
} // namespace nopiskl
