#include "services/judge_worker/judge_core.h"

#include "services/judge_worker/compile_service.h"
#include "services/judge_worker/run_service.h"
#include "services/judge_worker/judge_summary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace oj::worker {

namespace {

std::string read_text_file_if_exists(const std::filesystem::path& file_path) {
    std::ifstream input(file_path, std::ios::in | std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::vector<oj::protocol::TestCase> load_test_cases_from_problem_directory(std::int64_t problem_id) {
    std::vector<oj::protocol::TestCase> test_cases;
    const auto tests_directory = std::filesystem::path{"problems"} /
                                 std::to_string(problem_id) / "tests";
    if (!std::filesystem::exists(tests_directory)) {
        return test_cases;
    }

    for (std::size_t index = 1;; ++index) {
        const auto input_path = tests_directory / (std::to_string(index) + ".in");
        const auto output_path = tests_directory / (std::to_string(index) + ".out");
        if (!std::filesystem::exists(input_path) || !std::filesystem::exists(output_path)) {
            break;
        }

        oj::protocol::TestCase item;
        item.input = read_text_file_if_exists(input_path);
        item.expected_output = read_text_file_if_exists(output_path);
        test_cases.push_back(std::move(item));
    }

    return test_cases;
}

} // namespace

oj::protocol::JudgeResponse JudgeCore::judge(const oj::protocol::JudgeRequest& request) const {
    oj::protocol::JudgeResponse response;
    response.submission_id = request.submission_id;

    try {
        const auto work_directory = prepare_work_directory(request.submission_id);
        (void)work_directory;

        CompileService compile_service;
        const auto compile_result = compile_service.compile(
            work_directory,
            std::string{oj::protocol::to_string(request.language)},
            request.source_code);

        response.compile_success = compile_result.success;
        response.compile_stdout = compile_result.stdout_text;
        response.compile_stderr = compile_result.stderr_text;

        if (!response.compile_success) {
            response.final_status = oj::protocol::JudgeStatus::compile_error;
            if (response.compile_stderr.empty()) {
                response.compile_stderr = "compiler failed with exit code " + std::to_string(compile_result.exit_code);
            }
            return response;
        }

        const auto file_test_cases = load_test_cases_from_problem_directory(request.problem_id);
        const auto& effective_test_cases = file_test_cases.empty() ? request.test_cases : file_test_cases;

        for (const auto& test_case : effective_test_cases) {
            response.test_case_results.push_back(
                run_single_testcase(compile_result.executable_path.string(),
                                    test_case,
                                    request.time_limit_ms,
                                    request.memory_limit_mb));
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
                                                            const oj::protocol::TestCase& test_case,
                                                            std::int32_t time_limit_ms,
                                                            std::int32_t memory_limit_mb) const {
    RunService run_service;
    const auto run_work_directory = std::filesystem::path{"runtime"} / "judge_worker" / "runs";
    std::filesystem::create_directories(run_work_directory);
    const auto run_result = run_service.run(executable_path,
                                            test_case.input,
                                            time_limit_ms,
                                            memory_limit_mb,
                                            run_work_directory);

    oj::protocol::TestCaseResult result;
    result.input = test_case.input;
    result.expected_output = test_case.expected_output;
    result.actual_output = run_result.stdout_text;
    result.time_used_ms = run_result.time_used_ms;
    result.memory_used_kb = run_result.memory_used_kb;

    if (run_result.status != oj::protocol::JudgeStatus::ok) {
        result.status = run_result.status;
        result.error_message = run_result.error_message;
        if (!run_result.stderr_text.empty()) {
            if (!result.error_message.empty()) {
                result.error_message += " | ";
            }
            result.error_message += run_result.stderr_text;
        }
        return result;
    }

    if (normalize_output(result.actual_output) == normalize_output(test_case.expected_output)) {
        result.status = oj::protocol::JudgeStatus::ok;
    } else {
        result.status = oj::protocol::JudgeStatus::wrong_answer;
        result.error_message = "actual output does not match expected output";
    }

    return result;
}

void JudgeCore::summarize_results(oj::protocol::JudgeResponse& response) const {
    summarize_judge_response(response);
}

std::string JudgeCore::normalize_output(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

} // namespace oj::worker

