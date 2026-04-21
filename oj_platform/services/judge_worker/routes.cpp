#include "services/judge_worker/routes.h"

#include "common/protocol_json.h"
#include "common/protocol.hpp"
#include "services/judge_worker/judge_core.h"

#include <crow.h>
#include <stdexcept>
#include <string>

namespace oj::worker {

namespace {

crow::response json_error(int code, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return crow::response{code, body};
}

oj::protocol::JudgeRequest parse_judge_request(const crow::request& req) {
    return oj::common::deserialize_judge_request(req.body);
}

} // namespace

void register_routes(crow::Crow<>& app, const WorkerAppContext& context) {
    CROW_ROUTE(app, "/")([] {
        return crow::response{200, "judge_worker is running"};
    });

    CROW_ROUTE(app, "/api/health")([service_name = std::string{context.config.service_name}] {
        crow::json::wvalue body;
        body["service"] = service_name;
        body["status"] = "ok";
        return crow::response{body};
    });

    auto judge_core = context.judge_core;
    CROW_ROUTE(app, "/api/judge").methods(crow::HTTPMethod::POST)([judge_core](const crow::request& req) {
        if (!judge_core) {
            return json_error(500, "judge_core dependency is not initialized");
        }

        try {
            const auto request = parse_judge_request(req);
            const auto result = judge_core->judge(request);
            return crow::response{oj::common::serialize_judge_response(result)};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });
}

} // namespace oj::worker

