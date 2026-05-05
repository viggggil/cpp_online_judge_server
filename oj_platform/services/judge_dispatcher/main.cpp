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
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dispatcher = oj::dispatcher;

namespace {

volatile std::sig_atomic_t g_running = 1;

struct PendingJudgeTask {
    std::string submission_id;
    std::future<oj::common::SubmissionResult> future;
};

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

oj::common::SubmissionResult build_not_found_result(
    oj::common::SubmissionResult record,
    const std::string& problem_id_text) {
    record.status = "NOT_FOUND";
    record.accepted = false;
    record.detail = "problem " + problem_id_text + " does not exist";
    return record;
}

oj::common::SubmissionResult execute_judge_task(
    dispatcher::WorkerPool& worker_pool,
    std::string submission_id,
    std::string problem_id_text,
    std::string language,
    std::string source_code) {
    auto record = load_submission_record(submission_id);

    try {
        oj::server::ProblemRepository repository;
        const auto problem_id = parse_problem_id(problem_id_text);
        const auto detail = repository.find_detail(problem_id);
        if (!detail) {
            return build_not_found_result(std::move(record), problem_id_text);
        }

        oj::protocol::JudgeRequest judge_request;
        const auto numeric_submission_id =
            std::stoll(submission_id.substr(submission_id.find('-') + 1));
        judge_request.submission_id = numeric_submission_id;
        judge_request.problem_id = problem_id;
        judge_request.language = parse_language(language);
        judge_request.source_code = source_code;
        judge_request.time_limit_ms = detail->time_limit_ms;
        judge_request.memory_limit_mb = detail->memory_limit_mb;

        for (const auto& ref : repository.load_test_case_refs(problem_id)) {
            oj::protocol::TestCase test_case;
            test_case.input_object_key = ref.input_object_key;
            test_case.output_object_key = ref.output_object_key;
            test_case.input_sha256 = ref.input_sha256;
            test_case.output_sha256 = ref.output_sha256;
            test_case.input_size_bytes = ref.input_size_bytes;
            test_case.output_size_bytes = ref.output_size_bytes;
            judge_request.test_cases.push_back(std::move(test_case));
        }

        record.judge_response = worker_pool.judge(judge_request);
        record.status = std::string{oj::protocol::to_string(record.judge_response.final_status)};
        record.accepted = (record.judge_response.final_status == oj::protocol::JudgeStatus::ok);
        record.detail = build_submission_detail(record.judge_response);
    } catch (const std::exception& ex) {
        if (std::string{ex.what()} == "problem not found") {
            return build_not_found_result(std::move(record), problem_id_text);
        }
        oj::dispatcher::mark_submission_system_error(record, ex.what());
    } catch (...) {
        oj::dispatcher::mark_submission_system_error(record, "unknown worker error");
    }

    return record;
}

void collect_finished_tasks(std::vector<PendingJudgeTask>& pending_tasks) {
    for (auto it = pending_tasks.begin(); it != pending_tasks.end();) {
        if (it->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        try {
            const auto result = it->future.get();
            save_submission_record(result);
        } catch (const std::exception& ex) {
            std::fprintf(
                stderr,
                "judge_dispatcher failed to finalize submission %s: %s\n",
                it->submission_id.c_str(),
                ex.what());
        } catch (...) {
            std::fprintf(
                stderr,
                "judge_dispatcher failed to finalize submission %s: unknown error\n",
                it->submission_id.c_str());
        }

        it = pending_tasks.erase(it);
    }
}

void wait_all_pending_tasks(std::vector<PendingJudgeTask>& pending_tasks) {
    while (!pending_tasks.empty()) {
        collect_finished_tasks(pending_tasks);
        if (!pending_tasks.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void dispatch_task(
    dispatcher::WorkerPool& worker_pool,
    std::vector<PendingJudgeTask>& pending_tasks,
    const std::string& payload) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("invalid queue payload");
    }

    const std::string submission_id =
        json.has("submission_id") ? std::string{json["submission_id"].s()} : std::string{};
    const std::string problem_id_text =
        json.has("problem_id") ? std::string{json["problem_id"].s()} : std::string{};
    const std::string language =
        json.has("language") ? std::string{json["language"].s()} : std::string{"cpp"};
    const std::string source_code =
        json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};

    auto record = load_submission_record(submission_id);
    record.status = "RUNNING";
    record.accepted = false;
    record.detail = "submission is being judged";
    save_submission_record(record);

    pending_tasks.push_back(PendingJudgeTask{
        submission_id,
        std::async(
            std::launch::async,
            [&worker_pool, submission_id, problem_id_text, language, source_code]() mutable {
                return execute_judge_task(
                    worker_pool,
                    std::move(submission_id),
                    std::move(problem_id_text),
                    std::move(language),
                    std::move(source_code));
            })
    });
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
    std::vector<PendingJudgeTask> pending_tasks;
    const auto max_inflight_tasks =
        std::max<std::size_t>(1, worker_pool.size());

    while (g_running) {
        try {
            collect_finished_tasks(pending_tasks);

            if (pending_tasks.size() >= max_inflight_tasks) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            const auto payload = redis_client.blpop(redis_config.submission_queue_key, 1);
            if (!payload) {
                continue;
            }

            dispatch_task(worker_pool, pending_tasks, *payload);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "judge_dispatcher error: %s\n", ex.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    wait_all_pending_tasks(pending_tasks);
    return 0;
}
