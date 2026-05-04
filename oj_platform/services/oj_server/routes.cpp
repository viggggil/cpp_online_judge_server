#include "services/oj_server/routes.h"

#include "common/path_utils.h"
#include "services/oj_server/assignment_repository.h"
#include "services/oj_server/auth_service.h"
#include "services/oj_server/judge_service.h"
#include "services/oj_server/problem_repository.h"
#include "services/oj_server/redis_client.h"
#include "services/oj_server/problem_importer.h"

#include <crow.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

namespace oj::server {

namespace {

constexpr auto kProblemListCacheKey = "oj:problems:list";
constexpr auto kAssignmentListCacheKey = "oj:assignments:list";

std::int64_t unix_now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string assignment_detail_cache_key(std::int64_t assignment_id) {
    return "oj:assignments:detail:" + std::to_string(assignment_id);
}

void invalidate_assignment_cache(
    const RedisClient& redis_client,
    std::int64_t assignment_id) {
    redis_client.del(kAssignmentListCacheKey);
    if (assignment_id > 0) {
        redis_client.del(assignment_detail_cache_key(assignment_id));
    }
}

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

std::optional<AuthenticatedUser> require_admin(const crow::request& req) {
  auto user = require_user(req);
  if (!user) {
    return std::nullopt;
  }
  if (!user->is_admin()) {
    return std::nullopt;
  }
  return user;
}

std::optional<std::int64_t> find_user_id_by_username(
    const MySqlClient& mysql_client,
    const std::string& username) {
    if (username.empty()) {
        return std::nullopt;
    }

    auto connection = mysql_client.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement("SELECT id FROM users WHERE username = ?")
    };
    statement->setString(1, username);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next()) {
        return std::nullopt;
    }

    return result->getInt64("id");
}

crow::response json_error(int code, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return crow::response{code, body};
}

crow::json::wvalue make_auth_json(const std::string& token,
                                  const std::string& username,
                                  const std::string& role) {
  crow::json::wvalue body;
  body["token"] = token;
  body["username"] = username;
  body["role"] = role;
  body["is_admin"] = role == "admin";
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

// 把题目详情转换成前端展示接口使用的 JSON 结构。
crow::json::wvalue make_problem_detail_json(const oj::protocol::ProblemDetail& problem) {
    crow::json::wvalue body;
    body["id"] = problem.id;
    body["title"] = problem.title;
    body["statement_markdown"] = problem.statement_markdown;
    body["time_limit_ms"] = problem.time_limit_ms;
    body["memory_limit_mb"] = problem.memory_limit_mb;

    crow::json::wvalue::list tags;
    for (const auto& tag : problem.tags) {
        tags.push_back(crow::json::wvalue(tag));
    }
    body["tags"] = std::move(tags);
    return body;
}

// 把一次提交的完整判题结果转换成前端可直接消费的 JSON 响应。
crow::json::wvalue make_submission_json(const oj::common::SubmissionResult& result) {
    crow::json::wvalue body;
    body["submission_id"] = result.submission_id;
    body["username"] = result.username;
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
        item["input"] = tc.input;
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

// 生成提交列表接口所需的轻量摘要 JSON，避免返回完整判题明细。
crow::json::wvalue make_submission_list_json(const std::vector<oj::common::SubmissionListItem>& submissions) {
    crow::json::wvalue::list items;
    for (const auto& submission : submissions) {
        crow::json::wvalue item;
        item["submission_id"] = submission.submission_id;
        item["problem_id"] = submission.problem_id;
        item["language"] = submission.language;
        item["status"] = submission.status;
        item["final_status"] = submission.final_status;
        item["accepted"] = submission.accepted;
        item["detail"] = submission.detail;
        item["created_at"] = submission.created_at;
        item["total_time_used_ms"] = submission.total_time_used_ms;
        item["peak_memory_used_kb"] = submission.peak_memory_used_kb;
        items.push_back(std::move(item));
    }

    crow::json::wvalue body;
    body["submissions"] = std::move(items);
    return body;
}

// 生成题目列表接口 JSON，并与 Redis 缓存结构保持一致。
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

crow::json::wvalue make_assignment_summary_json(
    const AssignmentSummary& assignment) {
    crow::json::wvalue item;
    item["id"] = assignment.id;
    item["title"] = assignment.title;
    item["start_at"] = assignment.start_at;
    item["end_at"] = assignment.end_at;
    item["created_by"] = assignment.created_by;
    item["created_at"] = assignment.created_at;
    item["updated_at"] = assignment.updated_at;
    item["problem_count"] = assignment.problem_count;
    return item;
}

crow::json::wvalue make_assignment_list_json(
    const std::vector<AssignmentSummary>& assignments) {
    crow::json::wvalue::list items;
    for (const auto& assignment : assignments) {
        items.push_back(make_assignment_summary_json(assignment));
    }

    crow::json::wvalue body;
    body["assignments"] = std::move(items);
    return body;
}

crow::json::wvalue make_assignment_detail_json(
    const AssignmentDetail& detail) {
    crow::json::wvalue body;
    body["id"] = detail.id;
    body["title"] = detail.title;
    body["description_markdown"] = detail.description_markdown;
    body["start_at"] = detail.start_at;
    body["end_at"] = detail.end_at;
    body["created_by"] = detail.created_by;
    body["created_at"] = detail.created_at;
    body["updated_at"] = detail.updated_at;

    crow::json::wvalue::list problems;
    for (const auto& problem : detail.problems) {
        crow::json::wvalue item;
        item["problem_id"] = problem.problem_id;
        item["title"] = problem.title;
        item["display_order"] = problem.display_order;
        item["alias"] = problem.alias;
        problems.push_back(std::move(item));
    }
    body["problems"] = std::move(problems);
    return body;
}

} // namespace

// 统一注册静态页面、鉴权接口、题目接口、提交接口和管理员后台接口。
void register_routes(crow::Crow<>& app) {
    CROW_ROUTE(app, "/")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "home.html"));
    });

    CROW_ROUTE(app, "/problems")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "problems.html"));
    });

    CROW_ROUTE(app, "/problems/<int>")([](std::int64_t) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "problem.html"));
    });

    CROW_ROUTE(app, "/submit/<int>")([](std::int64_t) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "submit.html"));
    });

    CROW_ROUTE(app, "/submissions")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "submissions.html"));
    });

    CROW_ROUTE(app, "/submissions/<string>")([](const std::string&) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "submission.html"));
    });

    CROW_ROUTE(app, "/assignments")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "assignments.html"));
    });

    CROW_ROUTE(app, "/assignments/<int>")([](std::int64_t) {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "assignment.html"));
    });

    CROW_ROUTE(app, "/web/admin-assignment-create.html")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "admin-assignment-create.html"));
    });

    CROW_ROUTE(app, "/web/admin-assignment-edit.html")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "admin-assignment-edit.html"));
    });

    CROW_ROUTE(app, "/web/admin-problem-create.html")([] {
        return serve_file(resolve_web_path(std::filesystem::path{"web"} / "admin-problem-create.html"));
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

    CROW_ROUTE(app, "/api/assignments").methods(crow::HTTPMethod::GET)([] {
        try {
            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};

            if (const auto cached = redis_client.get(kAssignmentListCacheKey); cached) {
                crow::response resp;
                resp.code = 200;
                resp.set_header("Content-Type", "application/json; charset=utf-8");
                resp.set_header("X-Cache", "HIT");
                resp.body = *cached;
                return resp;
            }

            AssignmentRepository repository;
            const auto body = make_assignment_list_json(repository.list_assignments());
            const auto payload = body.dump();

            redis_client.setex(
                kAssignmentListCacheKey,
                redis_config.assignment_list_ttl_seconds,
                payload);

            crow::response resp;
            resp.code = 200;
            resp.set_header("Content-Type", "application/json; charset=utf-8");
            resp.set_header("X-Cache", "MISS");
            resp.body = payload;
            return resp;
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/assignments/<int>").methods(crow::HTTPMethod::GET)([](
        std::int64_t assignment_id) {
        try {
            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            const auto cache_key = assignment_detail_cache_key(assignment_id);

            if (const auto cached = redis_client.get(cache_key); cached) {
                crow::response resp;
                resp.code = 200;
                resp.set_header("Content-Type", "application/json; charset=utf-8");
                resp.set_header("X-Cache", "HIT");
                resp.body = *cached;
                return resp;
            }

            AssignmentRepository repository;
            const auto detail = repository.find_assignment_detail(assignment_id);
            if (!detail) {
                return json_error(404, "assignment not found");
            }

            const auto body = make_assignment_detail_json(*detail);
            const auto payload = body.dump();

            redis_client.setex(
                cache_key,
                redis_config.assignment_detail_ttl_seconds,
                payload);

            crow::response resp;
            resp.code = 200;
            resp.set_header("Content-Type", "application/json; charset=utf-8");
            resp.set_header("X-Cache", "MISS");
            resp.body = payload;
            return resp;
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
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
            return crow::response{200, make_auth_json(token, username, "user")};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/auth/admin/register").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto json = crow::json::load(req.body);
        if (!json || !json.has("username") || !json.has("password") ||
            !json.has("admin_code")) {
            return json_error(400, "username, password and admin_code are required");
        }

        try {
            const std::string username = json["username"].s();
            const std::string password = json["password"].s();
            const std::string admin_code = json["admin_code"].s();

            AuthService auth_service;
            const auto token = auth_service.register_admin(username, password, admin_code);

            return crow::response{200, make_auth_json(token, username, "admin")};
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
            const auto authenticated_user = auth_service.verify_token(token);
            const auto role = authenticated_user ? authenticated_user->role : "user";
            return crow::response{200, make_auth_json(token, username, role)};
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
        body["role"] = user->role;
        body["is_admin"] = user->is_admin();
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
        if (!json || !json.has("problem_id") || !json.has("language") ||
            !json.has("source_code")) {
            return json_error(400, "problem_id, language and source_code are required");
        }

        try {
            oj::common::SubmissionRequest request{
                json["problem_id"].s(),
                json["language"].s(),
                json["source_code"].s()};

            if (json.has("assignment_id")) {
                const std::string assignment_id_text = json["assignment_id"].s();
                if (!assignment_id_text.empty()) {
                    const auto assignment_id = std::stoll(assignment_id_text);
                    const auto problem_id = std::stoll(request.problem_id);

                    AssignmentRepository repository;
                    const auto detail = repository.find_assignment_detail(assignment_id);
                    if (!detail) {
                        return json_error(404, "assignment not found");
                    }

                    bool contains_problem = false;
                    for (const auto& problem : detail->problems) {
                        if (problem.problem_id == problem_id) {
                            contains_problem = true;
                            break;
                        }
                    }
                    if (!contains_problem) {
                        return json_error(400, "problem does not belong to assignment");
                    }

                    if (unix_now_seconds() < detail->start_at) {
                        return json_error(400, "assignment has not started");
                    }
                }
            }

            JudgeService judge_service;
            const auto result = judge_service.submit(user->username, request);
            return crow::response{202, make_submission_json(result)};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/submissions").methods(crow::HTTPMethod::GET)([](const crow::request& req) {
        const auto user = require_user(req);
        if (!user) {
            return json_error(401, "please login first");
        }

        JudgeService judge_service;
        return crow::response{200, make_submission_list_json(judge_service.list_submissions(user->username))};
    });

    CROW_ROUTE(app, "/api/submissions/<string>")([](const crow::request& req, const std::string& submission_id) {
        const auto user = require_user(req);
        if (!user) {
            return json_error(401, "please login first");
        }

        JudgeService judge_service;
        const auto result = judge_service.find_submission(user->username, submission_id);
        if (!result) {
            return json_error(404, "submission not found");
        }
        return crow::response{200, make_submission_json(*result)};
    });

    CROW_ROUTE(app, "/api/admin/problems/<int>/statement").methods(crow::HTTPMethod::GET)([](const crow::request& req, std::int64_t problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        try {
            const std::string language = "zh-CN";
            ProblemRepository repository;
            const auto statement = repository.find_statement_markdown(problem_id, language);
            if (!statement) {
                return json_error(404, "problem statement not found");
            }
            crow::json::wvalue body;
            body["problem_id"] = problem_id;
            body["language"] = language;
            body["statement_markdown"] = *statement;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/problems/<int>/statement").methods(crow::HTTPMethod::PUT)([](const crow::request& req, std::int64_t problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json || !json.has("statement_markdown")) {
            return json_error(400, "statement_markdown is required");
        }

        try {
            const std::string language =
                json.has("language") ? std::string{json["language"].s()} : "zh-CN";

            const std::string statement_markdown = json["statement_markdown"].s();

            if (statement_markdown.empty()) {
                return json_error(400, "statement_markdown cannot be empty");
            }

            if (statement_markdown.size() > 1024 * 1024) {
                return json_error(400, "statement_markdown is too large");
            }
            ProblemRepository repository;
            repository.update_statement_markdown(problem_id, language, statement_markdown);
            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = problem_id;
            body["language"] = language;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });
    CROW_ROUTE(app, "/api/admin/problems/<int>/id").methods(crow::HTTPMethod::PUT)([](const crow::request& req, std::int64_t old_problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json || !json.has("new_problem_id")) {
            return json_error(400, "new_problem_id is required");
        }

        try {
            const auto new_problem_id = json["new_problem_id"].i();

            ProblemRepository repository;
            repository.update_problem_id(old_problem_id, new_problem_id);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            redis_client.del(kProblemListCacheKey);

            crow::json::wvalue body;
            body["ok"] = true;
            body["old_problem_id"] = old_problem_id;
            body["new_problem_id"] = new_problem_id;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });
    CROW_ROUTE(app, "/api/admin/problems/<int>").methods(crow::HTTPMethod::DELETE)([](const crow::request& req, std::int64_t problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        try {
            ProblemRepository repository;
            repository.delete_problem(problem_id);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            redis_client.del(kProblemListCacheKey);

            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = problem_id;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/problems/<int>/title").methods(crow::HTTPMethod::PUT)([](const crow::request& req, std::int64_t problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json || !json.has("title")) {
            return json_error(400, "title is required");
        }

        try {
            const std::string title = json["title"].s();

            if (title.empty()) {
                return json_error(400, "title cannot be empty");
            }

            if (title.size() > 128) {
                return json_error(400, "title is too long");
            }

            ProblemRepository repository;
            repository.update_problem_title(problem_id, title);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            redis_client.del(kProblemListCacheKey);

            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = problem_id;
            body["title"] = title;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/problems/<int>/limits").methods(crow::HTTPMethod::PUT)([](const crow::request& req, std::int64_t problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json || !json.has("time_limit_ms") || !json.has("memory_limit_mb")) {
            return json_error(400, "time_limit_ms and memory_limit_mb are required");
        }

        try {
            const auto time_limit_ms = json["time_limit_ms"].i();
            const auto memory_limit_mb = json["memory_limit_mb"].i();

            if (time_limit_ms <= 0 || time_limit_ms > 60000) {
                return json_error(400, "time_limit_ms must be between 1 and 60000");
            }

            if (memory_limit_mb <= 0 || memory_limit_mb > 4096) {
                return json_error(400, "memory_limit_mb must be between 1 and 4096");
            }

            ProblemRepository repository;
            repository.update_problem_limits(problem_id, time_limit_ms, memory_limit_mb);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            redis_client.del(kProblemListCacheKey);

            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = problem_id;
            body["time_limit_ms"] = time_limit_ms;
            body["memory_limit_mb"] = memory_limit_mb;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/problems/import").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        if (req.body.empty()) {
            return json_error(400, "zip body is required");
        }

        try {
            int sample_count = 2;
            const auto sample_count_param = req.url_params.get("sample_count");
            if (sample_count_param != nullptr) {
                sample_count = std::stoi(sample_count_param);
            }
            if (sample_count < 0 || sample_count > 100) {
                return json_error(400, "invalid sample_count");
            }

            ProblemImporter importer;
            const auto result = importer.import_zip_body(req.body, sample_count);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            redis_client.del(kProblemListCacheKey);
            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = result.problem_id;
            body["title"] = result.title;
            body["testcase_count"] = result.testcase_count;
            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });
    
    CROW_ROUTE(app, "/api/admin/problems/<int>/testcases/file").methods(crow::HTTPMethod::POST)([](const crow::request& req, std::int64_t problem_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto filename_param = req.url_params.get("filename");
        if (filename_param == nullptr || std::string{filename_param}.empty()) {
            return json_error(400, "filename is required");
        }

        if (req.body.empty()) {
            return json_error(400, "file body is required");
        }

        try {
            ProblemImporter importer;
            const auto result = importer.append_testcase_file_body(
                problem_id,
                filename_param,
                req.body);

            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = result.problem_id;
            body["case_no"] = result.case_no;
            body["filename"] = result.filename;
            body["paired"] = result.paired;
            body["message"] = result.message;

            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/problems").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json ||
            !json.has("problem_id") ||
            !json.has("title") ||
            !json.has("statement_markdown") ||
            !json.has("time_limit_ms") ||
            !json.has("memory_limit_mb")) {
            return json_error(400, "problem_id, title, statement_markdown, time_limit_ms and memory_limit_mb are required");
        }

        try {
            const auto problem_id = json["problem_id"].i();
            const std::string title = json["title"].s();
            const std::string statement_markdown = json["statement_markdown"].s();
            const auto time_limit_ms = json["time_limit_ms"].i();
            const auto memory_limit_mb = json["memory_limit_mb"].i();

            ProblemRepository repository;
            repository.create_problem(
                problem_id,
                title,
                time_limit_ms,
                memory_limit_mb,
                statement_markdown);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            redis_client.del(kProblemListCacheKey);

            crow::json::wvalue body;
            body["ok"] = true;
            body["problem_id"] = problem_id;
            body["title"] = title;
            body["time_limit_ms"] = time_limit_ms;
            body["memory_limit_mb"] = memory_limit_mb;

            return crow::response{201, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });
    CROW_ROUTE(app, "/api/admin/assignments").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json ||
            !json.has("title") ||
            !json.has("description_markdown") ||
            !json.has("start_at") ||
            !json.has("end_at") ||
            !json.has("problem_ids")) {
            return json_error(
                400,
                "title, description_markdown, start_at, end_at and problem_ids are required");
        }

        try {
            CreateAssignmentRequest request;
            request.title = json["title"].s();
            request.description_markdown = json["description_markdown"].s();
            request.start_at = json["start_at"].i();
            request.end_at = json["end_at"].i();
            const MySqlClient mysql_client;
            const auto created_by = find_user_id_by_username(mysql_client, admin->username);
            if (!created_by) {
                return json_error(400, "admin user not found");
            }
            request.created_by = *created_by;

            for (const auto& item : json["problem_ids"]) {
                AssignmentProblemInput problem;
                problem.problem_id = item.i();
                request.problems.push_back(std::move(problem));
            }

            AssignmentRepository repository;
            const auto assignment_id = repository.create_assignment(request);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            invalidate_assignment_cache(redis_client, assignment_id);

            crow::json::wvalue body;
            body["ok"] = true;
            body["assignment_id"] = assignment_id;
            body["title"] = request.title;
            body["problem_count"] = static_cast<int>(request.problems.size());

            return crow::response{201, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/assignments/<int>").methods(crow::HTTPMethod::PATCH)([](const crow::request& req, std::int64_t assignment_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json) {
            return json_error(400, "invalid json body");
        }

        try {
            AssignmentRepository repository;
            const auto current_detail = repository.find_assignment_detail(assignment_id);
            if (!current_detail) {
                return json_error(404, "assignment not found");
            }

            UpdateAssignmentRequest request;

            if (json.has("title")) {
                const std::string title = json["title"].s();
                if (title != current_detail->title) {
                    request.title = title;
                }
            }

            if (json.has("description_markdown")) {
                const std::string description_markdown = json["description_markdown"].s();
                if (description_markdown != current_detail->description_markdown) {
                    request.description_markdown = description_markdown;
                }
            }

            if (json.has("start_at")) {
                const auto start_at = json["start_at"].i();
                if (start_at != current_detail->start_at) {
                    request.start_at = start_at;
                }
            }

            if (json.has("end_at")) {
                const auto end_at = json["end_at"].i();
                if (end_at != current_detail->end_at) {
                    request.end_at = end_at;
                }
            }

            if (request.title ||
                request.description_markdown ||
                request.start_at ||
                request.end_at) {
                repository.update_assignment(assignment_id, request);
            }

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            invalidate_assignment_cache(redis_client, assignment_id);

            const auto detail = repository.find_assignment_detail(assignment_id);
            if (!detail) {
                return json_error(404, "assignment not found");
            }

            crow::json::wvalue body;
            body["ok"] = true;
            body["assignment_id"] = assignment_id;
            body["title"] = detail->title;
            body["start_at"] = detail->start_at;
            body["end_at"] = detail->end_at;

            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });

    CROW_ROUTE(app, "/api/admin/assignments/<int>/problems").methods(crow::HTTPMethod::POST)([](const crow::request& req, std::int64_t assignment_id) {
        const auto admin = require_admin(req);
        if (!admin) {
            return json_error(403, "admin only");
        }

        const auto json = crow::json::load(req.body);
        if (!json || !json.has("problem_ids")) {
            return json_error(400, "problem_ids is required");
        }

        try {
            std::vector<AssignmentProblemInput> problems;

            const auto problem_ids = json["problem_ids"];
            for (std::size_t i = 0; i < problem_ids.size(); ++i) {
                AssignmentProblemInput item;
                item.problem_id = problem_ids[i].i();
                problems.push_back(std::move(item));
            }

            AssignmentRepository repository;
            repository.add_assignment_problems(assignment_id, problems);

            const oj::common::RedisConfig redis_config{};
            RedisClient redis_client{redis_config};
            invalidate_assignment_cache(redis_client, assignment_id);

            const auto detail = repository.find_assignment_detail(assignment_id);
            if (!detail) {
                return json_error(404, "assignment not found");
            }

            crow::json::wvalue body;
            body["ok"] = true;
            body["assignment_id"] = assignment_id;
            body["added_count"] = static_cast<int>(problems.size());
            body["problem_count"] = static_cast<int>(detail->problems.size());

            return crow::response{200, body};
        } catch (const std::exception& ex) {
            return json_error(400, ex.what());
        }
    });



}

} // namespace oj::server
