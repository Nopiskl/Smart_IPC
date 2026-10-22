#include "application.h"
#include "admin_setup.h"
#include "options.h"
#include "nopiskl/config/config.h"
#include "nopiskl/common/files.h"
#include "nopiskl/control/control.h"
#include "nopiskl/media/factory.h"
#include "nopiskl/web/auth.h"
#include "nopiskl/web/server.h"
#include <chrono>
#include <thread>

namespace nopiskl {
namespace smartipc {
int run_application(int argc, char **argv, const volatile std::sig_atomic_t &stop_requested) {
    const auto options = parse_options(argc, argv);
    if (options.help) {
        print_help();
        return 0;
    }
    make_dirs(options.data);
    if (options.initialize) {
        initialize_administrator(options.data, stop_requested);
        return 0;
    }
    const auto config = load_config(options.defaults, options.data);
    auto http = config.at("http");
    if (options.port != 0) {
        http["port"] = options.port;
    }
    // 声明顺序确保异常退出时也先销毁 Server，再销毁 Control 和 Auth。
    Auth auth(options.data);
    Control control(config, options.data, make_media(true), make_motor(options.simulate_motor));
    Server server(control, auth, options.web);
    control.start();
    server.start(http);
    while (!stop_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    server.stop();
    control.stop();
    return 0;
}
} // namespace smartipc
} // namespace nopiskl
