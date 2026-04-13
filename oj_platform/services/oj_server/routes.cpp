#include "services/oj_server/routes.h"

#include "services/oj_server/judge_service.h"
#include "services/oj_server/problem_repository.h"

#include <crow.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace oj::server {

namespace {

std::filesystem::path resolve_web_path(const std::filesystem::path& relative_path) {
    if (std::filesystem::exists(relative_path)) {
        return relative_path;
    }

    const auto nested = std::filesystem::path{"oj_platform"} / relative_path;
    if (std::filesystem::exists(nested)) {
        return nested;
    }

    return relative_path;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string guess_content_type(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".md") return "text/plain; charset=utf-8";
    return "text/plain; charset=utf-8";
}

crow::response serve_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return crow::response{404, "file not found"};
    }

    crow::response resp;
    resp.code = 200;
    resp.set_header("Content-Type", guess_content_type(path));
    resp.body = read_text_file(path);
    return resp;
}

crow::json::wvalue make_problem_detail_json(const oj::protocol::ProblemDetail& problem) {
    crow::json::wvalue body;
    body["id"] = problem.id;
    body["title"] = problem.title;
    body["statement"] = problem.statement;
    body["time_limit_ms"] = problem.time_limit_ms;
    body["memory_limit_mb"] = problem.memory_limit_mb;

    crow::json::wvalue::list tags;
    for (const auto& tag : problem.tags) {
        tags.push_back(crow::json::wvalue(tag));
    }
    body["tags"] = std::move(tags);

    crow::json::wvalue::list samples;
    for (const auto& sample : problem.samples) {
        crow::json::wvalue item;
        item["input"] = sample.input;
        item["output"] = sample.output;
        samples.push_back(std::move(item));
    }
    body["samples"] = std::move(samples);
    return body;
}

crow::json::wvalue make_submission_json(const oj::common::SubmissionResult& result) {
    crow::json::wvalue body;
    body["submission_id"] = result.submission_id;
    body["problem_id"] = result.problem_id;
    body["language"] = result.language;
    body["status"] = result.status;
    body["accepted"] = result.accepted;
    body["detail"] = result.detail;

    crow::json::wvalue judge;
    judge["submission_id"] = result.judge_response.submission_id;
    judge["final_status"] = std::string{oj::protocol::to_string(result.judge_response.final_status)};
    judge["compile_success"] = result.judge_response.compile_success;
    judge["compile_stdout"] = result.judge_response.compile_stdout;
    judge["compile_stderr"] = result.judge_response.compile_stderr;
    judge["total_time_used_ms"] = result.judge_response.total_time_used_ms;
    judge["peak_memory_used_kb"] = result.judge_response.peak_memory_used_kb;
    judge["system_message"] = result.judge_response.system_message;

    crow::json::wvalue::list items;
    for (const auto& tc : result.judge_response.test_case_results) {
        crow::json::wvalue item;
        item["status"] = std::string{oj::protocol::to_string(tc.status)};
        item["time_used_ms"] = tc.time_used_ms;
        item["memory_used_kb"] = tc.memory_used_kb;
        item["actual_output"] = tc.actual_output;
        item["expected_output"] = tc.expected_output;
        item["error_message"] = tc.error_message;
        items.push_back(std::move(item));
    }
    judge["test_case_results"] = std::move(items);
    body["judge_response"] = std::move(judge);
    return body;
}

} // namespace

void register_routes(crow::Crow<>& app) {
    CROW_ROUTE(app, "/")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "index.html"));
    });

    CROW_ROUTE(app, "/problems/<int>")([](std::int64_t) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "problem.html"));
    });

    CROW_ROUTE(app, "/submit/<int>")([](std::int64_t) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "submit.html"));
    });

    CROW_ROUTE(app, "/submissions/<string>")([](const std::string&) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "submission.html"));
    });

    CROW_ROUTE(app, "/web/<path>")([](const std::string& file_path) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / file_path));
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

    CROW_ROUTE(app, "/api/problems/<int>")([](std::int64_t problem_id) {
        ProblemRepository repository;
        const auto detail = repository.find_detail(problem_id);
        if (!detail) {
            return crow::response{404, "problem not found"};
        }
        return crow::response{make_problem_detail_json(*detail)};
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
        return crow::response{200, make_submission_json(result)};
    });

    CROW_ROUTE(app, "/api/submissions/<string>")([](const std::string& submission_id) {
        JudgeService judge_service;
        const auto result = judge_service.find_submission(submission_id);
        if (!result) {
            return crow::response{404, "submission not found"};
        }
        return crow::response{200, make_submission_json(*result)};
    });
}

} // namespace oj::server

