#pragma once
#include "nopiskl/common/types.h"
struct mg_context;
struct mg_connection;
namespace nopiskl {
class Auth;
class Control;
class Server {
public:
    Server(Control &control, Auth &auth, std::string web_root);
    ~Server();
    void start(const Json &http);
    void stop();

private:
    Control &control_;
    Auth &auth_;
    std::string web_root_;
    mg_context *context_ = nullptr;
    static int dispatch(mg_connection *connection);
    int handle(mg_connection *connection);
};
} // namespace nopiskl
