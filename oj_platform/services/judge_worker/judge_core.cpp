#include "services/judge_worker/judge_core.h"

#include "common/object_storage_client.h"
#include "services/judge_worker/compile_service.h"
#include "services/judge_worker/run_service.h"
#include "services/judge_worker/judge_summary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

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

std::string read_text_file(const std::filesystem::path& file_path) {
    std::ifstream input(file_path, std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + file_path.string());
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

void verify_cached_file(const std::filesystem::path& file_path,
                        const std::string& expected_sha256,
                        std::int64_t expected_size_bytes) {
    if (expected_size_bytes > 0) {
        const auto actual_size = oj::common::file_size_bytes(file_path);
        if (actual_size != expected_size_bytes) {
            throw std::runtime_error(
                "cached file size mismatch: " + file_path.string());
        }
    }

    if (!expected_sha256.empty()) {
        const auto actual_sha256 = oj::common::sha256_file(file_path);
        if (actual_sha256 != expected_sha256) {
            throw std::runtime_error(
                "cached file sha256 mismatch: " + file_path.string());
        }
    }
}

void ensure_cached_object(const oj::common::ObjectStorageClient& storage_client,
                          const std::string& object_key,
                          const std::filesystem::path& cache_path,
                          const std::string& expected_sha256,
                          std::int64_t expected_size_bytes) {
    auto try_verify = [&]() {
        if (!std::filesystem::exists(cache_path)) {
            return false;
        }
        verify_cached_file(cache_path, expected_sha256, expected_size_bytes);
        return true;
    };

    try {
        if (try_verify()) {
            return;
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(cache_path, ignored);
    }

    storage_client.download_file(object_key, cache_path);
    verify_cached_file(cache_path, expected_sha256, expected_size_bytes);
}

oj::protocol::TestCase materialize_test_case_from_object_storage(
    const oj::protocol::TestCase& testcase_ref,
    const std::filesystem::path& cache_directory,
    std::size_t case_index) {
    oj::common::ObjectStorageClient storage_client;
    oj::protocol::TestCase materialized = testcase_ref;

    const auto input_cache_path = cache_directory /
        (testcase_ref.input_sha256.empty()
             ? ("case_" + std::to_string(case_index + 1) + ".in")
             : (testcase_ref.input_sha256 + ".in"));
    const auto output_cache_path = cache_directory /
        (testcase_ref.output_sha256.empty()
             ? ("case_" + std::to_string(case_index + 1) + ".out")
             : (testcase_ref.output_sha256 + ".out"));

    if (!materialized.input_object_key.empty()) {
        ensure_cached_object(storage_client,
                             materialized.input_object_key,
                             input_cache_path,
                             materialized.input_sha256,
                             materialized.input_size_bytes);
        materialized.input = read_text_file(input_cache_path);
    }

    if (!materialized.output_object_key.empty()) {
        ensure_cached_object(storage_client,
                             materialized.output_object_key,
                             output_cache_path,
                             materialized.output_sha256,
                             materialized.output_size_bytes);
        materialized.expected_output = read_text_file(output_cache_path);
    }

    return materialized;
}

} // namespace

// 串起编译、逐点评测和结果汇总，是 judge_worker 的核心判题入口。
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

        const std::vector<oj::protocol::TestCase>* effective_test_cases = &request.test_cases;
        std::vector<oj::protocol::TestCase> fallback_test_cases;

        if (effective_test_cases->empty()) {
            // Dispatcher/OJ Server 下发的测试点是权威数据源。
            // 仅在兼容历史本地目录模式、且请求未携带测试点时，才回退到磁盘读取。
            fallback_test_cases = load_test_cases_from_problem_directory(request.problem_id);
            effective_test_cases = &fallback_test_cases;
        }

        const auto test_cache_directory = std::filesystem::path{"runtime"} / "judge_worker" / "object_cache";
        std::filesystem::create_directories(test_cache_directory);

        for (std::size_t index = 0; index < effective_test_cases->size(); ++index) {
            const auto test_case = materialize_test_case_from_object_storage(
                (*effective_test_cases)[index],
                test_cache_directory,
                index);
            response.test_case_results.push_back(
                run_single_testcase(compile_result.executable_path,
                                    work_directory,
                                    response.test_case_results.size(),
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

// 为每次提交创建独立工作目录，避免不同提交之间的中间文件互相污染。
std::filesystem::path JudgeCore::prepare_work_directory(std::int64_t submission_id) const {
    auto work_directory = std::filesystem::path{"runtime"} / "judge_worker" /
                          ("submission_" + std::to_string(submission_id));
    std::error_code ec;
    std::filesystem::remove_all(work_directory, ec);
    std::filesystem::create_directories(work_directory);
    return work_directory;
}

// 执行单个测试点并把运行结果转换成统一的判题明细结构。
oj::protocol::TestCaseResult JudgeCore::run_single_testcase(const std::filesystem::path& executable_path,
                                                            const std::filesystem::path& submission_work_directory,
                                                            std::size_t case_index,
                                                            const oj::protocol::TestCase& test_case,
                                                            std::int32_t time_limit_ms,
                                                            std::int32_t memory_limit_mb) const {
    RunService run_service;
    const auto run_work_directory = submission_work_directory / ("testcase_" + std::to_string(case_index + 1));
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
