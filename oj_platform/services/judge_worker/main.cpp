#include "common/platform_config.h"
#include "services/judge_worker/judge_core.h"
#include "services/judge_worker/routes.h"

#include <crow.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

int load_port_from_env_or_default(int default_port) {
    if (const char* value = std::getenv("OJ_JUDGE_WORKER_PORT")) {
        try {
            return std::stoi(value);
        } catch (...) {
            return default_port;
        }
    }
    return default_port;
}

oj::common::ServiceConfig load_config() {
    return {load_port_from_env_or_default(18081), "judge_worker"};
}

} // namespace

int main() {
    const auto config = load_config();

    auto judge_core = std::make_shared<oj::worker::JudgeCore>();
    oj::worker::WorkerAppContext context{config, std::move(judge_core)};

    crow::SimpleApp app;
    oj::worker::register_routes(app, context);

    CROW_LOG_INFO << config.service_name << " listening on port " << config.port;
    app.port(config.port).multithreaded().run();
    return 0;
}

