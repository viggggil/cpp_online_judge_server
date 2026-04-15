#include "services/oj_server/routes.h"

#include "common/path_utils.h"
#include "services/oj_server/auth_service.h"
#include "services/oj_server/judge_service.h"
#include "services/oj_server/problem_repository.h"
#include "services/oj_server/redis_client.h"

#include <crow.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace oj::server {

namespace {

constexpr auto kProblemListCacheKey = "oj:problems:list";

std::string extract_bearer_token(const crow::request& req) {
    const auto auth_header = req.get_header_value("Authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (auth_header.size() <= prefix.size() || auth_header.rfind(prefix.data(), 0) != 0) {
        return {};
    }
    return auth_header.substr(prefix.size());
}

std::optional<AuthenticatedUser> require_user(const crow::request& req) {
    AuthService auth_service;
    return auth_service.verify_token(extract_bearer_token(req));
}

crow::response json_error(int code, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return crow::response{code, body};
}

crow::json::wvalue make_auth_json(const std::string& token, const std::string& username) {
    crow::json::wvalue body;
    body["token"] = token;
    body["username"] = username;
    return body;
}

std::filesystem::path resolve_web_path(const std::filesystem::path& relative_path) {
    return oj::common::resolve_project_path(relative_path);
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

crow::json::wvalue make_problem_list_json(const std::vector<oj::common::ProblemSummary>& problems) {
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
        const oj::common::RedisConfig redis_config{};
        RedisClient redis_client{redis_config};

        if (const auto cached = redis_client.get(kProblemListCacheKey); cached) {
            crow::response resp;
            resp.code = 200;
            resp.set_header("Content-Type", "application/json; charset=utf-8");
            resp.set_header("X-Cache", "HIT");
            resp.body = *cached;
            return resp;
        }

        ProblemRepository repository;
        const auto problems = repository.list();
        const auto body = make_problem_list_json(problems);
        const auto payload = body.dump();

        redis_client.setex(kProblemListCacheKey, redis_config.problem_list_ttl_seconds, payload);

        crow::response resp;
        resp.code = 200;
        resp.set_header("Content-Type", "application/json; charset=utf-8");
        resp.set_header("X-Cache", "MISS");
        resp.body = payload;
        return resp;
    });

    CROW_ROUTE(app, "/api/auth/register").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto json = crow::json::load(req.body);
        if (!json || !json.has("username") || !json.has("password")) {
            return json_error(400, "username and password are required");
        }

        try {
            const std::string username = json["username"].s();
            const std::string password = json["password"].s();
            AuthService auth_service;
            const auto token = auth_service.register_user(username, password);
            return crow::response{200, make_auth_json(token, username)};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/auth/login").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto json = crow::json::load(req.body);
        if (!json || !json.has("username") || !json.has("password")) {
            return json_error(400, "username and password are required");
        }

        try {
            const std::string username = json["username"].s();
            const std::string password = json["password"].s();
            AuthService auth_service;
            const auto token = auth_service.login_user(username, password);
            return crow::response{200, make_auth_json(token, username)};
        } catch (const std::exception& ex) {
            return json_error(401, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/auth/me")([](const crow::request& req) {
        const auto user = require_user(req);
        if (!user) {
            return json_error(401, "please login first");
        }

        crow::json::wvalue body;
        body["username"] = user->username;
        return crow::response{200, body};
    });

    CROW_ROUTE(app, "/api/problems/<int>").methods(crow::HTTPMethod::GET)([](const crow::request& req, std::int64_t problem_id) {
        const auto user = require_user(req);
        if (!user) {
            return json_error(401, "please login first");
        }

        ProblemRepository repository;
        const auto detail = repository.find_detail(problem_id);
        if (!detail) {
            return json_error(404, "problem not found");
        }
        return crow::response{make_problem_detail_json(*detail)};
    });

    CROW_ROUTE(app, "/api/submissions").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto user = require_user(req);
        if (!user) {
            return json_error(401, "please login first");
        }

        const auto json = crow::json::load(req.body);
        if (!json) {
            return json_error(400, "invalid json payload");
        }

        oj::common::SubmissionRequest request{
            json["problem_id"].s(),
            json["language"].s(),
            json["source_code"].s()};

        JudgeService judge_service;
        const auto result = judge_service.submit(request);
        return crow::response{200, make_submission_json(result)};
    });

    CROW_ROUTE(app, "/api/submissions/<string>")([](const crow::request& req, const std::string& submission_id) {
        const auto user = require_user(req);
        if (!user) {
            return json_error(401, "please login first");
        }

        JudgeService judge_service;
        const auto result = judge_service.find_submission(submission_id);
        if (!result) {
            return json_error(404, "submission not found");
        }
        return crow::response{200, make_submission_json(*result)};
    });
}

} // namespace oj::server

