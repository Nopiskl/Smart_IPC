#pragma once
#include <string>

namespace nopiskl {
namespace smartipc {
struct Options {
    std::string defaults = "./config/default.json";
    std::string data = "./data";
    std::string web = "./web";
    bool initialize = false;
    bool simulate_motor = false;
    bool help = false;
    int port = 0;
};

Options parse_options(int argc, char **argv);
void print_help();
} // namespace smartipc
} // namespace nopiskl
