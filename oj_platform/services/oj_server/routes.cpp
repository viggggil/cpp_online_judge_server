#include "services/oj_server/routes.h"

#include "services/oj_server/judge_service.h"
#include "services/oj_server/problem_repository.h"

#include <crow.h>

namespace oj::server {

void register_routes(crow::Crow<>& app) {
    CROW_ROUTE(app, "/")([] {
        return crow::response{200, "oj_server is running"};
    });

    CROW_ROUTE(app, "/api/health")([] {
        crow::json::wvalue body;
        body["service"] = "oj_server";
        body["status"] = "ok";
        return crow::response{body};
    });

    CROW_ROUTE(app, "/api/problems")([] {
        ProblemRepository repository;
        const auto problems = repository.list();

        crow::json::wvalue::list items;
        for (const auto& problem : problems) {
            crow::json::wvalue item;
            item["id"] = problem.id;
            item["title"] = problem.title;
            item["difficulty"] = problem.difficulty;
            items.push_back(std::move(item));
        }

        crow::json::wvalue body;
        body["problems"] = std::move(items);
        return crow::response{body};
    });

    CROW_ROUTE(app, "/api/submissions").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto json = crow::json::load(req.body);
        if (!json) {
            return crow::response{400, "invalid json payload"};
        }

        oj::common::SubmissionRequest request{
            json["problem_id"].s(),
            json["language"].s(),
            json["source_code"].s()};

        JudgeService judge_service;
        const auto result = judge_service.submit(request);

        crow::json::wvalue body;
        body["submission_id"] = result.submission_id;
        body["status"] = result.status;
        body["detail"] = result.detail;
        return crow::response{202, body};
    });
}

} // namespace oj::server

