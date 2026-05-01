#include "services/oj_server/judge_service.h"

#include "services/oj_server/problem_repository.h"
#include "services/oj_server/redis_client.h"
#include "services/oj_server/submission_repository.h"

#include <crow/json.h>

#include <chrono>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace oj::server {

namespace {

std::int64_t parse_problem_id(const std::string& problem_id_text) {
    return std::stoll(problem_id_text);
}

oj::protocol::LanguageType parse_language(const std::string& language_text) {
    if (language_text == "cpp" || language_text == "c++" || language_text == "cpp17") {
        return oj::protocol::LanguageType::cpp;
    }
    throw std::runtime_error("unsupported language: " + language_text);
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

} // namespace

// 创建提交记录、校验题目存在性并把判题任务投递到 Redis 队列中等待调度。
oj::common::SubmissionResult JudgeService::submit(const std::string& username,
                                                  const oj::common::SubmissionRequest& request) const {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto submission_id = "sub-" + std::to_string(tick);

    oj::common::SubmissionResult result;
    result.submission_id = submission_id;
    result.username = username;
    result.problem_id = request.problem_id;
    result.language = request.language;
    result.source_code = request.source_code;

    try {
        ProblemRepository repository;
        SubmissionRepository submission_repository;
        const auto problem_id = parse_problem_id(request.problem_id);
        const auto detail = repository.find_detail(problem_id);
        if (!detail) {
            result.status = "NOT_FOUND";
            result.detail = "problem " + request.problem_id + " does not exist";
            submission_repository.create_submission(result.submission_id, username, request,
                                                    result.status, result.detail);
            return result;
        }

        result.status = "QUEUED";
        result.accepted = false;
        result.detail = build_submission_detail(result.status);

        submission_repository.create_submission(result.submission_id, username, request,
                                                result.status, result.detail);

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

    try {
        SubmissionRepository submission_repository;
        submission_repository.update_submission(result);
    } catch (...) {
    }

    return result;
}

// 查询指定用户的一次提交，并把持久化状态整理成前端可直接展示的结果。
std::optional<oj::common::SubmissionResult> JudgeService::find_submission(const std::string& username,
                                                                          const std::string& submission_id) const {
    SubmissionRepository submission_repository;
    const auto stored = submission_repository.find_submission_for_user(submission_id, username);
    if (!stored) {
        return std::nullopt;
    }

    auto result = stored->result;
    if (result.detail.empty()) {
        result.detail = build_submission_detail(result.status);
    }
    if (!is_terminal_submission_status(result.status)) {
        result.accepted = false;
    }
    return result;
}

// 按用户维度列出最近提交记录，供提交列表页面直接展示。
std::vector<oj::common::SubmissionListItem> JudgeService::list_submissions(const std::string& username) const {
    SubmissionRepository submission_repository;
    return submission_repository.list_submissions_for_user(username);
}

} // namespace oj::server
