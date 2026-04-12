#include "services/judge_worker/judge_core.h"

#include "services/judge_worker/compile_service.h"
#include "services/judge_worker/run_service.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace oj::worker {

oj::protocol::JudgeResponse JudgeCore::judge(const oj::protocol::JudgeRequest& request) const {
    oj::protocol::JudgeResponse response;
    response.submission_id = request.submission_id;

    try {
        const auto work_directory = prepare_work_directory(request.submission_id);
        (void)work_directory;

        CompileService compile_service;
        const auto executable_path = compile_service.compile(
            std::string{oj::protocol::to_string(request.language)}, request.source_code);

        response.compile_success = !executable_path.empty();
        response.compile_stdout = "compile finished";

        if (!response.compile_success) {
            response.final_status = oj::protocol::JudgeStatus::compile_error;
            response.compile_stderr = "compiler returned empty artifact path";
            return response;
        }

        for (const auto& test_case : request.test_cases) {
            response.test_case_results.push_back(run_single_testcase(executable_path, test_case));
        }

        summarize_results(response);
    } catch (const std::exception& ex) {
        response.final_status = oj::protocol::JudgeStatus::system_error;
        response.system_message = ex.what();
    } catch (...) {
        response.final_status = oj::protocol::JudgeStatus::system_error;
        response.system_message = "unknown system error";
    }

    return response;
}

std::filesystem::path JudgeCore::prepare_work_directory(std::int64_t submission_id) const {
    auto work_directory = std::filesystem::path{"runtime"} / "judge_worker" /
                          ("submission_" + std::to_string(submission_id));
    std::filesystem::create_directories(work_directory);
    return work_directory;
}

oj::protocol::TestCaseResult JudgeCore::run_single_testcase(const std::string& executable_path,
                                                            const oj::protocol::TestCase& test_case) const {
    RunService run_service;

    oj::protocol::TestCaseResult result;
    result.expected_output = test_case.expected_output;
    result.actual_output = run_service.run(executable_path);
    result.time_used_ms = 1;
    result.memory_used_kb = 64;

    if (normalize_output(result.actual_output) == normalize_output(test_case.expected_output)) {
        result.status = oj::protocol::JudgeStatus::ok;
    } else {
        result.status = oj::protocol::JudgeStatus::wrong_answer;
        result.error_message = "actual output does not match expected output";
    }

    return result;
}

void JudgeCore::summarize_results(oj::protocol::JudgeResponse& response) const {
    response.total_time_used_ms = 0;
    response.peak_memory_used_kb = 0;

    if (response.test_case_results.empty()) {
        response.final_status = oj::protocol::JudgeStatus::system_error;
        response.system_message = "no test cases provided";
        return;
    }

    response.final_status = oj::protocol::JudgeStatus::ok;
    for (const auto& item : response.test_case_results) {
        response.total_time_used_ms += item.time_used_ms;
        response.peak_memory_used_kb = std::max(response.peak_memory_used_kb, item.memory_used_kb);

        if (item.status != oj::protocol::JudgeStatus::ok &&
            response.final_status == oj::protocol::JudgeStatus::ok) {
            response.final_status = item.status;
        }
    }
}

std::string JudgeCore::normalize_output(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

} // namespace oj::worker

