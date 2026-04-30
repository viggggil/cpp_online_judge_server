#pragma once

#include "common/platform_types.h"
#include "common/protocol.hpp"
#include "services/oj_server/mysql_client.h"

#include <optional>
#include <vector>
#include<string>

namespace oj::server {

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
    std::vector<oj::common::ProblemSummary> list() const;
    std::vector<oj::protocol::ProblemMeta> list_problem_meta() const;
    std::optional<oj::protocol::ProblemDetail> find_detail(std::int64_t problem_id) const;
    std::vector<oj::protocol::TestCase> load_test_cases(std::int64_t problem_id) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
