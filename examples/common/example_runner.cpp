#include "example_runner.h"
#include "nopiskl/media/factory.h"
#include "nopiskl/config/config.h"
#include "nopiskl/common/files.h"
#include "nopiskl/media/media.h"
#include <csignal>
#include <iostream>
namespace {
volatile std::sig_atomic_t stop_requested = 0;
void stop(int) {
    stop_requested = 1;
}
} // namespace
namespace nopiskl {
int run_example(int argc, char **argv, bool capture) {
    try {
        std::string path = "./config/default.json";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--config" && i + 1 < argc) {
                path = argv[++i];
            } else if (arg == "--help") {
                std::cout << "nopiskl RTSP example [--config PATH]\nStop with SIGINT/SIGTERM.\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
            }
        }
        auto config = Json::parse(read_file(path));
        config["ai"]["enabled"] = false;
        validate_config(config);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::signal(SIGPIPE, SIG_IGN);
        auto media = make_media(false, capture);
        media->configure(config);
        std::cout << "[nopiskl] RTSP at rtsp://<device-ip>:" << config["video"]["rtsp_port"]
                  << "/live/0\n";
        while (!stop_requested) {
            media->step();
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "[nopiskl] " << e.what() << '\n';
        return 1;
    }
}
} // namespace nopiskl
