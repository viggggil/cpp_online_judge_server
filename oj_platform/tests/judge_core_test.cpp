#include "services/judge_worker/judge_core.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

std::string unique_suffix() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    std::random_device rd;
    std::mt19937_64 rng(rd());

    return std::to_string(now) + "_" + std::to_string(rng());
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to write file: " + path.string());
    }

    output << content;
    if (!output.good()) {
        throw std::runtime_error("failed to flush file: " + path.string());
    }
}

class TempDirectory {
public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() / ("oj_judge_core_test_" + unique_suffix())) {
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : original_path_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::error_code ignored;
        std::filesystem::current_path(original_path_, ignored);
    }

private:
    std::filesystem::path original_path_;
};

oj::protocol::JudgeRequest make_sum_request(std::int64_t submission_id, std::int64_t problem_id) {
    oj::protocol::JudgeRequest request;
    request.submission_id = submission_id;
    request.problem_id = problem_id;
    request.language = oj::protocol::LanguageType::cpp;
    request.source_code =
        "#include <iostream>\n"
        "int main() {\n"
        "    long long a = 0;\n"
        "    long long b = 0;\n"
        "    std::cin >> a >> b;\n"
        "    std::cout << (a + b) << '\\n';\n"
        "    return 0;\n"
        "}\n";
    request.time_limit_ms = 1000;
    request.memory_limit_mb = 128;
    return request;
}

TEST(JudgeCoreTest, RequestTestCasesTakePrecedenceOverProblemDirectoryFixtures) {
    TempDirectory temp_directory;
    ScopedCurrentPath scoped_path{temp_directory.path()};

    const auto local_test_root = temp_directory.path() / "problems" / "424242" / "tests";
    write_text_file(local_test_root / "1.in", "40 2\n");
    write_text_file(local_test_root / "1.out", "999\n");

    auto request = make_sum_request(1, 424242);
    request.test_cases = {{"1 2\n", "3\n"}};

    oj::worker::JudgeCore judge_core;
    const auto response = judge_core.judge(request);

    EXPECT_EQ(response.final_status, oj::protocol::JudgeStatus::ok);
    ASSERT_EQ(response.test_case_results.size(), 1u);
    EXPECT_EQ(response.test_case_results[0].input, "1 2\n");
    EXPECT_EQ(response.test_case_results[0].expected_output, "3\n");
    EXPECT_EQ(response.test_case_results[0].actual_output, "3\n");
}

TEST(JudgeCoreTest, ObjectStorageBackedTestCasesCanBeReadFromLocalCacheFiles) {
    TempDirectory temp_directory;
    ScopedCurrentPath scoped_path{temp_directory.path()};

    const auto cache_directory = temp_directory.path() / "runtime" / "judge_worker" / "object_cache";
    const auto input_path = cache_directory / "sha-in.in";
    const auto output_path = cache_directory / "sha-out.out";
    write_text_file(input_path, "8 9\n");
    write_text_file(output_path, "17\n");

    auto request = make_sum_request(3, 616161);
    request.test_cases = {{
        "",
        "",
        "objects/case_1.in",
        "objects/case_1.out",
        "sha-in",
        "sha-out",
        4,
        3,
    }};

    oj::worker::JudgeCore judge_core;
    const auto response = judge_core.judge(request);

    EXPECT_EQ(response.final_status, oj::protocol::JudgeStatus::ok);
    ASSERT_EQ(response.test_case_results.size(), 1u);
    EXPECT_EQ(response.test_case_results[0].input, "8 9\n");
    EXPECT_EQ(response.test_case_results[0].expected_output, "17\n");
    EXPECT_EQ(response.test_case_results[0].actual_output, "17\n");
}

TEST(JudgeCoreTest, ProblemDirectoryIsUsedAsLegacyFallbackWhenRequestHasNoTestCases) {
    TempDirectory temp_directory;
    ScopedCurrentPath scoped_path{temp_directory.path()};

    const auto local_test_root = temp_directory.path() / "problems" / "515151" / "tests";
    write_text_file(local_test_root / "1.in", "4 5\n");
    write_text_file(local_test_root / "1.out", "9\n");

    auto request = make_sum_request(2, 515151);

    oj::worker::JudgeCore judge_core;
    const auto response = judge_core.judge(request);

    EXPECT_EQ(response.final_status, oj::protocol::JudgeStatus::ok);
    ASSERT_EQ(response.test_case_results.size(), 1u);
    EXPECT_EQ(response.test_case_results[0].input, "4 5\n");
    EXPECT_EQ(response.test_case_results[0].expected_output, "9\n");
    EXPECT_EQ(response.test_case_results[0].actual_output, "9\n");
}

} // namespace
