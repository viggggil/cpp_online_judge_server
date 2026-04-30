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
        std::string input_data;
        std::string expected_output;
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

class ProblemRepository {
public:
    ProblemRepository();
    explicit ProblemRepository(MySqlClient mysql_client);
    void update_problem_id(std::int64_t old_problem_id, std::int64_t new_problem_id) const;
    void delete_problem(std::int64_t problem_id) const;
    void update_problem_title(std::int64_t problem_id, const std::string& title) const;
    std::optional<std::string> find_statement_markdown(
    std::int64_t problem_id,
    const std::string& language = "zh-CN") const;

    void update_statement_markdown(
    std::int64_t problem_id,
    const std::string& language,
    const std::string& statement_markdown) const;
    std::int64_t allocate_problem_id(std::int64_t start_id) const;
    void import_problem(const ImportedProblem& problem) const;
    std::vector<oj::common::ProblemSummary> list() const;
    std::vector<oj::protocol::ProblemMeta> list_problem_meta() const;
    std::optional<oj::protocol::ProblemDetail> find_detail(std::int64_t problem_id) const;
    std::vector<oj::protocol::TestCase> load_test_cases(std::int64_t problem_id) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
