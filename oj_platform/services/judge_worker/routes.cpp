#include "services/judge_worker/routes.h"

#include "common/protocol.hpp"
#include "services/judge_worker/judge_core.h"

#include <crow.h>
#include <stdexcept>
#include <string>

namespace oj::worker {

namespace {

oj::protocol::LanguageType parse_language(const crow::json::rvalue& json) {
    if (!json.has("language")) {
        return oj::protocol::LanguageType::cpp;
    }

    const std::string text = json["language"].s();
    if (text == "cpp" || text == "c++" || text == "cpp17") {
        return oj::protocol::LanguageType::cpp;
    }
    throw std::runtime_error("unsupported language: " + text);
}

crow::response json_error(int code, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return crow::response{code, body};
}

crow::json::wvalue build_judge_response_json(const oj::protocol::JudgeResponse& result) {
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
    return body;
}

oj::protocol::JudgeRequest parse_judge_request(const crow::request& req) {
    const auto json = crow::json::load(req.body);
    if (!json) {
        throw std::runtime_error("invalid json payload");
    }

    oj::protocol::JudgeRequest request;
    request.submission_id = json.has("submission_id") ? json["submission_id"].i() : 0;
    request.problem_id = json.has("problem_id") ? json["problem_id"].i() : 0;
    request.language = parse_language(json);
    request.source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};
    request.time_limit_ms = json.has("time_limit_ms") ? json["time_limit_ms"].i() : request.time_limit_ms;
    request.memory_limit_mb = json.has("memory_limit_mb") ? json["memory_limit_mb"].i() : request.memory_limit_mb;

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
    return request;
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
            return crow::response{build_judge_response_json(result)};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });
}

} // namespace oj::worker

