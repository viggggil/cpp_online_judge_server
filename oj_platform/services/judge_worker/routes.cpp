#include "services/judge_worker/routes.h"

#include "services/judge_worker/judge_core.h"

#include <crow.h>

namespace oj::worker {

void register_routes(crow::Crow<>& app) {
    CROW_ROUTE(app, "/")([] {
        return crow::response{200, "judge_worker is running"};
    });

    CROW_ROUTE(app, "/api/health")([] {
        crow::json::wvalue body;
        body["service"] = "judge_worker";
        body["status"] = "ok";
        return crow::response{body};
    });

    CROW_ROUTE(app, "/api/judge").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto json = crow::json::load(req.body);
        if (!json) {
            return crow::response{400, "invalid json payload"};
        }

        oj::common::SubmissionRequest request{
            json["problem_id"].s(),
            json["language"].s(),
            json["source_code"].s()};

        JudgeCore judge_core;
        const auto result = judge_core.judge(request);

        crow::json::wvalue body;
        body["submission_id"] = result.submission_id;
        body["status"] = result.status;
        body["detail"] = result.detail;
        return crow::response{200, body};
    });
}

} // namespace oj::worker

