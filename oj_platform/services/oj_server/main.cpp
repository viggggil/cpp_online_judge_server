#include "common/platform_config.h"
#include "services/oj_server/routes.h"

#include <crow.h>

#include <cstdlib>

namespace {

int load_port_from_env_or_default(int default_port) {
    return oj::common::env_int_or_default("OJ_SERVER_PORT", default_port);
}

} // namespace

int main() {
    const oj::common::ServiceConfig config{load_port_from_env_or_default(18080), "oj_server"};

    crow::SimpleApp app;
    oj::server::register_routes(app);

    CROW_LOG_INFO << config.service_name << " listening on port " << config.port;
    app.port(config.port).multithreaded().run();
    return 0;
}

