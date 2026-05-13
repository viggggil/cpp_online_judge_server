#include "common/platform_config.h"
#include "common/judge_task.h"
#include "common/platform_types.h"
#include "services/judge_dispatcher/dispatcher_utils.h"
#include "services/judge_dispatcher/worker_client.h"
#include "services/oj_server/problem_repository.h"
#include "services/oj_server/rabbitmq_client.h"
#include "services/oj_server/submission_repository.h"

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
    std::uint64_t delivery_tag{};
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
    std::string language){
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
        judge_request.source_code = record.source_code;
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

void collect_finished_tasks(
    std::vector<PendingJudgeTask>& pending_tasks,
    oj::common::RabbitMqClient& rabbitmq) {

    for (auto it = pending_tasks.begin(); it != pending_tasks.end();) {
        if (it->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        try {
            const auto result = it->future.get();
            save_submission_record(result);
            rabbitmq.ack(it->delivery_tag);
        } catch (const std::exception& ex) {
            std::fprintf(
                stderr,
                "judge_dispatcher failed to finalize submission %s: %s\n",
                it->submission_id.c_str(),
                ex.what()
            );
            rabbitmq.nack_requeue(it->delivery_tag);
        } catch (...) {
            std::fprintf(
                stderr,
                "judge_dispatcher failed to finalize submission %s: unknown error\n",
                it->submission_id.c_str()
            );

            rabbitmq.nack_requeue(it->delivery_tag);
        }

        it = pending_tasks.erase(it);
    }
}

void dispatch_task(
    dispatcher::WorkerPool& worker_pool,
    std::vector<PendingJudgeTask>& pending_tasks,
    const oj::common::RabbitMqDelivery& delivery) {

    const auto task = oj::common::judge_task_from_json(delivery.body);

    auto record = load_submission_record(task.submission_id);

    // 幂等保护：如果已经是终态，直接不重复评测。
    if (record.status == "OK" ||
        record.status == "COMPILE_ERROR" ||
        record.status == "RUNTIME_ERROR" ||
        record.status == "TIME_LIMIT_EXCEEDED" ||
        record.status == "MEMORY_LIMIT_EXCEEDED" ||
        record.status == "OUTPUT_LIMIT_EXCEEDED" ||
        record.status == "WRONG_ANSWER" ||
        record.status == "PRESENTATION_ERROR" ||
        record.status == "SYSTEM_ERROR" ||
        record.status == "NOT_FOUND") {
        return;
    }

    record.status = "RUNNING";
    record.accepted = false;
    record.detail = "submission is being judged";
    save_submission_record(record);

    pending_tasks.push_back(PendingJudgeTask{
        task.submission_id,
        delivery.delivery_tag,
        std::async(
            std::launch::async,
            [&worker_pool, task]() mutable {
                return execute_judge_task(
                    worker_pool,
                    task.submission_id,
                    task.problem_id,
                    task.language
                );
            }
        )
    });
}

} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const oj::common::RabbitMqConfig rabbit_config{};
    oj::common::RabbitMqClient rabbitmq{rabbit_config};
    if (!rabbitmq.available()) {
        std::fprintf(
            stderr,
            "judge_dispatcher: rabbitmq unavailable: %s\n",
            rabbitmq.last_error().c_str());
        return 1;
    }

    dispatcher::WorkerPool worker_pool{dispatcher::parse_worker_endpoints_from_env()};
    std::vector<PendingJudgeTask> pending_tasks;
    const auto max_inflight_tasks = std::max<std::size_t>(1, worker_pool.size());

    while (g_running) {
        try {
            collect_finished_tasks(pending_tasks, rabbitmq);

            if (pending_tasks.size() >= max_inflight_tasks) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            const auto delivery = rabbitmq.consume_one(1000);
            if (!delivery) {
                continue;
            }

            try {
                const auto task = oj::common::judge_task_from_json(delivery->body);
                const auto record = load_submission_record(task.submission_id);

                if (record.status == "OK" ||
                    record.status == "COMPILE_ERROR" ||
                    record.status == "RUNTIME_ERROR" ||
                    record.status == "TIME_LIMIT_EXCEEDED" ||
                    record.status == "MEMORY_LIMIT_EXCEEDED" ||
                    record.status == "OUTPUT_LIMIT_EXCEEDED" ||
                    record.status == "WRONG_ANSWER" ||
                    record.status == "PRESENTATION_ERROR" ||
                    record.status == "SYSTEM_ERROR" ||
                    record.status == "NOT_FOUND") {
                    rabbitmq.ack(delivery->delivery_tag);
                    continue;
                }

                dispatch_task(worker_pool, pending_tasks, *delivery);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "invalid judge task: %s\n", ex.what());
                rabbitmq.reject_dead(delivery->delivery_tag);
            }
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "judge_dispatcher error: %s\n", ex.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    while (!pending_tasks.empty()) {
        collect_finished_tasks(pending_tasks, rabbitmq);
        if (!pending_tasks.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    return 0;
}
