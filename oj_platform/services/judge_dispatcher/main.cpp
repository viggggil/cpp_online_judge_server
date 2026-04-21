#include "common/path_utils.h"
#include "common/platform_config.h"
#include "common/platform_types.h"
#include "services/judge_dispatcher/dispatcher_utils.h"
#include "services/judge_dispatcher/worker_client.h"
#include "services/oj_server/problem_repository.h"
#include "services/oj_server/redis_client.h"

#include <crow/json.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
}

std::filesystem::path resolve_runtime_path(std::filesystem::path path) {
    return oj::common::resolve_project_path(std::move(path));
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

void persist_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
    output << content;
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

oj::common::SubmissionResult parse_submission_record(const crow::json::rvalue& json) {
    oj::common::SubmissionResult result;
    result.submission_id = json.has("submission_id") ? std::string{json["submission_id"].s()} : std::string{};
    result.problem_id = json.has("problem_id") ? std::string{json["problem_id"].s()} : std::string{};
    result.language = json.has("language") ? std::string{json["language"].s()} : std::string{};
    result.source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};
    result.status = json.has("status") ? std::string{json["status"].s()} : std::string{};
    result.accepted = json.has("accepted") && json["accepted"].b();
    result.detail = json.has("detail") ? std::string{json["detail"].s()} : std::string{};
    return result;
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

oj::common::SubmissionResult load_submission_record(const std::filesystem::path& submissions_root,
                                                    const std::string& submission_id) {
    const auto json = load_json_file(submissions_root / submission_id / "result.json");
    if (!json) {
        throw std::runtime_error("submission record not found: " + submission_id);
    }
    return parse_submission_record(*json);
}

void save_submission_record(const std::filesystem::path& submissions_root,
                            const oj::common::SubmissionResult& result) {
    persist_text_file(submissions_root / result.submission_id / "result.json",
                      build_submission_record_json(result).dump());
}

void process_task(const std::string& payload,
                  const std::filesystem::path& submissions_root) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("invalid queue payload");
    }

    const std::string submission_id = json.has("submission_id") ? std::string{json["submission_id"].s()} : std::string{};
    const std::string problem_id_text = json.has("problem_id") ? std::string{json["problem_id"].s()} : std::string{};
    const std::string language = json.has("language") ? std::string{json["language"].s()} : std::string{"cpp"};
    const std::string source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};

    auto record = load_submission_record(submissions_root, submission_id);
    record.status = "RUNNING";
    record.detail = "submission is being judged";
    save_submission_record(submissions_root, record);

    oj::server::ProblemRepository repository;
    const auto problem_id = parse_problem_id(problem_id_text);
    const auto detail = repository.find_detail(problem_id);
    if (!detail) {
        record.status = "NOT_FOUND";
        record.detail = "problem " + problem_id_text + " does not exist";
        save_submission_record(submissions_root, record);
        return;
    }

    oj::protocol::JudgeRequest judge_request;
    const auto numeric_submission_id = std::stoll(submission_id.substr(submission_id.find('-') + 1));
    judge_request.submission_id = numeric_submission_id;
    judge_request.problem_id = problem_id;
    judge_request.language = parse_language(language);
    judge_request.source_code = source_code;
    judge_request.time_limit_ms = detail->time_limit_ms;
    judge_request.memory_limit_mb = detail->memory_limit_mb;
    judge_request.test_cases = repository.load_test_cases(problem_id);

    oj::dispatcher::WorkerClient worker_client{oj::common::JudgeWorkerEndpoint{}};
    try {
        record.judge_response = worker_client.judge(judge_request);
        record.status = std::string{oj::protocol::to_string(record.judge_response.final_status)};
        record.accepted = (record.judge_response.final_status == oj::protocol::JudgeStatus::ok);
        record.detail = build_submission_detail(record.judge_response);
    } catch (const std::exception& ex) {
        oj::dispatcher::mark_submission_system_error(record, ex.what());
    } catch (...) {
        oj::dispatcher::mark_submission_system_error(record, "unknown worker error");
    }

    save_submission_record(submissions_root, record);
}

} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const auto submissions_root = resolve_runtime_path("runtime/submissions");
    const oj::common::RedisConfig redis_config{};

    oj::server::RedisClient redis_client{redis_config};
    if (!redis_client.available()) {
        std::fprintf(stderr, "judge_dispatcher: redis unavailable\n");
        return 1;
    }

    while (g_running) {
        try {
            const auto payload = redis_client.blpop(redis_config.submission_queue_key, 1);
            if (!payload) {
                continue;
            }
            process_task(*payload, submissions_root);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "judge_dispatcher error: %s\n", ex.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    return 0;
}