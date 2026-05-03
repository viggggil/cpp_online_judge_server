#pragma once

#include "common/platform_types.h"
#include "common/protocol.hpp"
#include "services/oj_server/mysql_client.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oj::server {

struct ImportedProblem {
    struct TestCase {
        int case_no{};

        std::string input_object_key;
        std::string output_object_key;

        std::string input_sha256;
        std::string output_sha256;

        std::int64_t input_size_bytes{};
        std::int64_t output_size_bytes{};

        bool is_sample{false};
    };

    std::int64_t id{};
    std::string title;
    int time_limit_ms{};
    int memory_limit_mb{};
    std::string checker_type;
    std::string statement_markdown;
    std::vector<std::string> tags;
    std::vector<TestCase> testcases;
};


struct TestCaseObjectRef {
    int case_no{};

    std::string input_object_key;
    std::string output_object_key;

    std::string input_sha256;
    std::string output_sha256;

    std::int64_t input_size_bytes{};
    std::int64_t output_size_bytes{};
};

class ProblemRepository {
public:
    ProblemRepository();
    explicit ProblemRepository(MySqlClient mysql_client);

    void create_problem(
        std::int64_t problem_id,
        const std::string& title,
        int time_limit_ms,
        int memory_limit_mb,
        const std::string& statement_markdown) const;

    void update_problem_id(std::int64_t old_problem_id, std::int64_t new_problem_id) const;
    void delete_problem(std::int64_t problem_id) const;
    void update_problem_title(std::int64_t problem_id, const std::string& title) const;
    void update_problem_limits(std::int64_t problem_id, int time_limit_ms, int memory_limit_mb) const;

    std::optional<std::string> find_statement_markdown(
        std::int64_t problem_id,
        const std::string& language = "zh-CN") const;

    bool testcase_exists(std::int64_t problem_id, int case_no) const;
    void append_testcase(std::int64_t problem_id, const ImportedProblem::TestCase& testcase) const;

    void update_statement_markdown(
        std::int64_t problem_id,
        const std::string& language,
        const std::string& statement_markdown) const;

    std::int64_t allocate_problem_id(std::int64_t start_id) const;
    void import_problem(const ImportedProblem& problem) const;
    std::vector<oj::common::ProblemSummary> list() const;
    std::vector<oj::protocol::ProblemMeta> list_problem_meta() const;
    std::optional<oj::protocol::ProblemDetail> find_detail(std::int64_t problem_id) const;
    std::vector<TestCaseObjectRef> load_test_case_refs(std::int64_t problem_id) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
