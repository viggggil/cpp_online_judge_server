#include "services/judge_worker/routes.h"

#include "common/protocol.hpp"
#include "services/judge_worker/judge_core.h"

#include <crow.h>

namespace oj::worker {

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
        const auto json = crow::json::load(req.body);
        if (!json) {
            return crow::response{400, "invalid json payload"};
        }

        oj::protocol::JudgeRequest request;
        request.submission_id = json.has("submission_id") ? json["submission_id"].i() : 0;
        request.problem_id = json.has("problem_id") ? json["problem_id"].i() : 0;
        request.language = oj::protocol::LanguageType::cpp;
        request.source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};

        if (json.has("time_limit_ms")) {
            request.time_limit_ms = json["time_limit_ms"].i();
        }
        if (json.has("memory_limit_mb")) {
            request.memory_limit_mb = json["memory_limit_mb"].i();
        }

        if (json.has("test_cases") && json["test_cases"].t() == crow::json::type::List) {
            for (const auto& item : json["test_cases"]) {
                oj::protocol::TestCase test_case;
                if (item.has("input")) {
                    test_case.input = item["input"].s();
                }
                if (item.has("expected_output")) {
                    test_case.expected_output = item["expected_output"].s();
                }
                request.test_cases.push_back(std::move(test_case));
            }
        }

        if (!judge_core) {
            return crow::response{500, "judge_core dependency is not initialized"};
        }

        const auto result = judge_core->judge(request);

        crow::json::wvalue body;
        body["submission_id"] = result.submission_id;
        body["final_status"] = std::string{oj::protocol::to_string(result.final_status)};
        body["compile_success"] = result.compile_success;
        body["compile_stdout"] = result.compile_stdout;
        body["compile_stderr"] = result.compile_stderr;
        body["total_time_used_ms"] = result.total_time_used_ms;
        body["peak_memory_used_kb"] = result.peak_memory_used_kb;
        body["system_message"] = result.system_message;

        crow::json::wvalue::list items;
        for (const auto& tc : result.test_case_results) {
            crow::json::wvalue item;
            item["status"] = std::string{oj::protocol::to_string(tc.status)};
            item["input"] = tc.input;
            item["time_used_ms"] = tc.time_used_ms;
            item["memory_used_kb"] = tc.memory_used_kb;
            item["actual_output"] = tc.actual_output;
            item["expected_output"] = tc.expected_output;
            item["error_message"] = tc.error_message;
            items.push_back(std::move(item));
        }
        body["test_case_results"] = std::move(items);
        return crow::response{200, body};
    });
}

} // namespace oj::worker

