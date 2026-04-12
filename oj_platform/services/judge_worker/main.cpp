#include "common/platform_config.h"
#include "services/judge_worker/routes.h"

#include <crow.h>

int main() {
    constexpr oj::common::ServiceConfig config{18081, "judge_worker"};

    crow::SimpleApp app;
    oj::worker::register_routes(app);

    CROW_LOG_INFO << config.service_name << " listening on port " << config.port;
    app.port(config.port).multithreaded().run();
    return 0;
}

