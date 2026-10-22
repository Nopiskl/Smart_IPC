#include "nopiskl/common/system.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace nopiskl {
uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::string random_hex(size_t bytes) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("Cannot open OS random source");
    }
    std::string raw(bytes, '\0');
    size_t offset = 0;
    while (offset < bytes) {
        ssize_t n = read(fd, &raw[offset], bytes - offset);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            close(fd);
            throw std::runtime_error("Random source failed");
        }
        offset += static_cast<size_t>(n);
    }
    close(fd);
    const char *hex = "0123456789abcdef";
    std::string out;
    for (unsigned char c : raw) {
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}
Json memory_info() {
    Json memory = Json::object();
    std::ifstream file("/proc/meminfo");
    std::string key, rest;
    uint64_t value;
    while (file >> key >> value) {
        std::getline(file, rest);
        if (key == "MemTotal:" || key == "MemAvailable:" || key == "CmaTotal:" ||
            key == "CmaFree:") {
            memory[key.substr(0, key.size() - 1) + "_kib"] = value;
        }
    }
    return memory;
}
Json device_info() {
    struct utsname u {};
    uname(&u);
    Json interfaces = Json::array();
    struct ifaddrs *addrs = nullptr;
    if (getifaddrs(&addrs) == 0) {
        for (auto p = addrs; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET ||
                (p->ifa_flags & IFF_LOOPBACK)) {
                continue;
            }
            char ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(p->ifa_addr)->sin_addr, ip,
                      sizeof(ip));
            interfaces.push_back({{"name", p->ifa_name}, {"ipv4", ip}});
        }
        freeifaddrs(addrs);
    }
    return {{"brand", "nopiskl"},        {"product", "SmartIPC"}, {"version", "1.0.0"},
            {"architecture", u.machine}, {"kernel", u.release},   {"interfaces", interfaces}};
}
} // namespace nopiskl
