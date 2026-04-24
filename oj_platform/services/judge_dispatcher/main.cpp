#include "common/platform_config.h"
#include "common/platform_types.h"
#include "services/judge_dispatcher/dispatcher_utils.h"
#include "services/judge_dispatcher/worker_client.h"
#include "services/oj_server/problem_repository.h"
#include "services/oj_server/redis_client.h"
#include "services/oj_server/submission_repository.h"

#include <crow/json.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace dispatcher = oj::dispatcher;

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
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

oj::common::SubmissionResult load_submission_record(const std::string& submission_id) {
    oj::server::SubmissionRepository submission_repository;
    const auto stored = submission_repository.find_submission(submission_id);
    if (!stored) {
        throw std::runtime_error("submission record not found: " + submission_id);
    }
    return stored->result;
}

void save_submission_record(const oj::common::SubmissionResult& result) {
    oj::server::SubmissionRepository submission_repository;
    submission_repository.update_submission(result);
}

void process_task(dispatcher::WorkerPool& worker_pool, const std::string& payload) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("invalid queue payload");
    }

    const std::string submission_id = json.has("submission_id") ? std::string{json["submission_id"].s()} : std::string{};
    const std::string problem_id_text = json.has("problem_id") ? std::string{json["problem_id"].s()} : std::string{};
    const std::string language = json.has("language") ? std::string{json["language"].s()} : std::string{"cpp"};
    const std::string source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};

    auto record = load_submission_record(submission_id);
    record.status = "RUNNING";
    record.detail = "submission is being judged";
    save_submission_record(record);

    oj::server::ProblemRepository repository;
    const auto problem_id = parse_problem_id(problem_id_text);
    const auto detail = repository.find_detail(problem_id);
    if (!detail) {
        record.status = "NOT_FOUND";
        record.detail = "problem " + problem_id_text + " does not exist";
        save_submission_record(record);
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

    try {
        record.judge_response = worker_pool.judge(judge_request);
        record.status = std::string{oj::protocol::to_string(record.judge_response.final_status)};
        record.accepted = (record.judge_response.final_status == oj::protocol::JudgeStatus::ok);
        record.detail = build_submission_detail(record.judge_response);
    } catch (const std::exception& ex) {
        oj::dispatcher::mark_submission_system_error(record, ex.what());
    } catch (...) {
        oj::dispatcher::mark_submission_system_error(record, "unknown worker error");
    }

    save_submission_record(record);
}

} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const oj::common::RedisConfig redis_config{};

    oj::server::RedisClient redis_client{redis_config};
    if (!redis_client.available()) {
        std::fprintf(stderr, "judge_dispatcher: redis unavailable\n");
        return 1;
    }

    dispatcher::WorkerPool worker_pool{dispatcher::parse_worker_endpoints_from_env()};

    while (g_running) {
        try {
            const auto payload = redis_client.blpop(redis_config.submission_queue_key, 1);
            if (!payload) {
                continue;
            }
            process_task(worker_pool, *payload);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "judge_dispatcher error: %s\n", ex.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    return 0;
}
