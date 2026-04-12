#include "common/platform_config.h"
#include "services/oj_server/routes.h"

#include <crow.h>

int main() {
    constexpr oj::common::ServiceConfig config{18080, "oj_server"};

    crow::SimpleApp app;
    oj::server::register_routes(app);

    CROW_LOG_INFO << config.service_name << " listening on port " << config.port;
    app.port(config.port).multithreaded().run();
    return 0;
}

