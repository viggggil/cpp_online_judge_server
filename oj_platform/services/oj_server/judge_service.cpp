#include "services/oj_server/judge_service.h"

#include "common/path_utils.h"
#include "services/oj_server/problem_repository.h"
#include "services/oj_server/redis_client.h"

#include <crow/json.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace oj::server {

namespace {

std::filesystem::path resolve_runtime_path(std::filesystem::path path) {
    return oj::common::resolve_project_path(path);
}

std::int64_t parse_problem_id(const std::string& problem_id_text) {
    return std::stoll(problem_id_text);
}

oj::protocol::LanguageType parse_language(const std::string& language_text) {
    if (language_text == "cpp" || language_text == "c++" || language_text == "cpp17") {
        return oj::protocol::LanguageType::cpp;
    }
    throw std::runtime_error("unsupported language: " + language_text);
}

std::string judge_status_to_submission_status(oj::protocol::JudgeStatus status) {
    return std::string{oj::protocol::to_string(status)};
}

bool is_terminal_submission_status(const std::string& status) {
    return status == "OK" ||
           status == "COMPILE_ERROR" ||
           status == "RUNTIME_ERROR" ||
           status == "TIME_LIMIT_EXCEEDED" ||
           status == "MEMORY_LIMIT_EXCEEDED" ||
           status == "WRONG_ANSWER" ||
           status == "PRESENTATION_ERROR" ||
           status == "SYSTEM_ERROR" ||
           status == "NOT_FOUND";
}

std::string build_submission_detail(const oj::protocol::JudgeResponse& response) {
    std::ostringstream output;
    output << "submission judged with status " << oj::protocol::to_string(response.final_status);
    if (!response.system_message.empty()) {
        output << "; system_message=" << response.system_message;
    }
    if (!response.compile_stderr.empty()) {
        output << "; compile_stderr=" << response.compile_stderr;
    }
    return output.str();
}

std::string build_submission_detail(const std::string& status) {
    if (status == "QUEUED") {
        return "submission has been accepted and queued";
    }
    if (status == "RUNNING") {
        return "submission is being judged";
    }
    return "submission status updated to " + status;
}

void persist_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
    output << content;
}

crow::json::wvalue build_submission_record_json(const oj::common::SubmissionResult& result) {
    crow::json::wvalue body;
    body["submission_id"] = result.submission_id;
    body["problem_id"] = result.problem_id;
    body["language"] = result.language;
    body["source_code"] = result.source_code;
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

std::optional<crow::json::rvalue> load_json_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto json = crow::json::load(buffer.str());
    if (!json) {
        throw std::runtime_error("invalid submission result json: " + path.string());
    }
    return json;
}

oj::protocol::JudgeStatus parse_judge_status(const std::string& text) {
    if (text == "OK") return oj::protocol::JudgeStatus::ok;
    if (text == "COMPILE_ERROR") return oj::protocol::JudgeStatus::compile_error;
    if (text == "RUNTIME_ERROR") return oj::protocol::JudgeStatus::runtime_error;
    if (text == "TIME_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::time_limit_exceeded;
    if (text == "MEMORY_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::memory_limit_exceeded;
    if (text == "WRONG_ANSWER") return oj::protocol::JudgeStatus::wrong_answer;
    if (text == "PRESENTATION_ERROR") return oj::protocol::JudgeStatus::presentation_error;
    return oj::protocol::JudgeStatus::system_error;
}

oj::common::SubmissionResult parse_submission_record(const crow::json::rvalue& json) {
    oj::common::SubmissionResult result;
    result.submission_id = json.has("submission_id") ? std::string{json["submission_id"].s()} : std::string{};
    result.problem_id = json.has("problem_id") ? std::string{json["problem_id"].s()} : std::string{};
    result.language = json.has("language") ? std::string{json["language"].s()} : std::string{};
    result.source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};
    result.status = json.has("status") ? std::string{json["status"].s()} : std::string{};
    result.accepted = json.has("accepted") ? json["accepted"].b() : false;
    result.detail = json.has("detail") ? std::string{json["detail"].s()} : std::string{};

    if (json.has("judge_response")) {
        const auto& judge = json["judge_response"];
        result.judge_response.submission_id = judge.has("submission_id") ? judge["submission_id"].i() : 0;
        result.judge_response.final_status = judge.has("final_status")
                                                 ? parse_judge_status(judge["final_status"].s())
                                                 : oj::protocol::JudgeStatus::system_error;
        result.judge_response.compile_success = judge.has("compile_success") && judge["compile_success"].b();
        result.judge_response.compile_stdout = judge.has("compile_stdout") ? std::string{judge["compile_stdout"].s()} : std::string{};
        result.judge_response.compile_stderr = judge.has("compile_stderr") ? std::string{judge["compile_stderr"].s()} : std::string{};
        result.judge_response.total_time_used_ms = judge.has("total_time_used_ms") ? judge["total_time_used_ms"].i() : 0;
        result.judge_response.peak_memory_used_kb = judge.has("peak_memory_used_kb") ? judge["peak_memory_used_kb"].i() : 0;
        result.judge_response.system_message = judge.has("system_message") ? std::string{judge["system_message"].s()} : std::string{};

        if (judge.has("test_case_results") && judge["test_case_results"].t() == crow::json::type::List) {
            for (const auto& item : judge["test_case_results"]) {
                oj::protocol::TestCaseResult tc;
                tc.status = item.has("status") ? parse_judge_status(item["status"].s()) : oj::protocol::JudgeStatus::system_error;
                tc.time_used_ms = item.has("time_used_ms") ? item["time_used_ms"].i() : 0;
                tc.memory_used_kb = item.has("memory_used_kb") ? item["memory_used_kb"].i() : 0;
                tc.actual_output = item.has("actual_output") ? std::string{item["actual_output"].s()} : std::string{};
                tc.expected_output = item.has("expected_output") ? std::string{item["expected_output"].s()} : std::string{};
                tc.error_message = item.has("error_message") ? std::string{item["error_message"].s()} : std::string{};
                result.judge_response.test_case_results.push_back(std::move(tc));
            }
        }
    }

    return result;
}

} // namespace

JudgeService::JudgeService(std::filesystem::path problems_root,
                           std::filesystem::path submissions_root)
    : problems_root_(std::move(problems_root)),
      submissions_root_(resolve_runtime_path(std::move(submissions_root))) {}

oj::common::SubmissionResult JudgeService::submit(const oj::common::SubmissionRequest& request) const {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto submission_id = "sub-" + std::to_string(tick);

    oj::common::SubmissionResult result;
    result.submission_id = submission_id;
    result.problem_id = request.problem_id;
    result.language = request.language;
    result.source_code = request.source_code;

    try {
        ProblemRepository repository;
        const auto problem_id = parse_problem_id(request.problem_id);
        const auto detail = repository.find_detail(problem_id);
        if (!detail) {
            result.status = "NOT_FOUND";
            result.detail = "problem " + request.problem_id + " does not exist";
            persist_text_file(submissions_root_ / submission_id / "result.json",
                              build_submission_record_json(result).dump());
            return result;
        }

        result.status = "QUEUED";
        result.accepted = false;
        result.detail = build_submission_detail(result.status);

        persist_text_file(submissions_root_ / submission_id / "source.cpp", request.source_code);
        persist_text_file(submissions_root_ / submission_id / "result.json",
                          build_submission_record_json(result).dump());

        crow::json::wvalue task_json;
        task_json["submission_id"] = result.submission_id;
        task_json["problem_id"] = request.problem_id;
        task_json["language"] = request.language;
        task_json["source_code"] = request.source_code;

        const oj::common::RedisConfig redis_config{};
        RedisClient redis_client{redis_config};
        if (!redis_client.available()) {
            throw std::runtime_error("redis is unavailable, cannot enqueue submission");
        }
        if (!redis_client.rpush(redis_config.submission_queue_key, task_json.dump())) {
            throw std::runtime_error("failed to push submission into redis queue");
        }
    } catch (const std::exception& ex) {
        result.status = "SYSTEM_ERROR";
        result.detail = ex.what();
        result.judge_response.final_status = oj::protocol::JudgeStatus::system_error;
        result.judge_response.system_message = ex.what();
    } catch (...) {
        result.status = "SYSTEM_ERROR";
        result.detail = "unknown judge service error";
        result.judge_response.final_status = oj::protocol::JudgeStatus::system_error;
        result.judge_response.system_message = "unknown judge service error";
    }

    persist_text_file(submissions_root_ / submission_id / "source.cpp", request.source_code);
    persist_text_file(submissions_root_ / submission_id / "result.json",
                      build_submission_record_json(result).dump());
    return result;
}

std::optional<oj::common::SubmissionResult> JudgeService::find_submission(const std::string& submission_id) const {
    const auto json = load_json_file(submissions_root_ / submission_id / "result.json");
    if (!json) {
        return std::nullopt;
    }
    auto result = parse_submission_record(*json);
    if (result.detail.empty()) {
        result.detail = build_submission_detail(result.status);
    }
    if (!is_terminal_submission_status(result.status)) {
        result.accepted = false;
    }
    return result;
}

} // namespace oj::server

