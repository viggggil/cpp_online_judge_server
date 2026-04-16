#include "services/oj_server/problem_repository.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace oj::server {

namespace {

std::string infer_difficulty(const oj::protocol::ProblemMeta& meta) {
    if (std::find(meta.tags.begin(), meta.tags.end(), "hard") != meta.tags.end()) {
        return "hard";
    }
    if (std::find(meta.tags.begin(), meta.tags.end(), "medium") != meta.tags.end()) {
        return "medium";
    }
    return "easy";
}

std::vector<std::string> load_problem_tags(sql::Connection& connection, std::int64_t problem_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT tag FROM problem_tags WHERE problem_id = ? ORDER BY tag ASC")};
    statement->setInt64(1, problem_id);
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

    std::vector<std::string> tags;
    while (result->next()) {
        tags.push_back(result->getString("tag"));
    }
    return tags;
}

} // namespace

ProblemRepository::ProblemRepository()
    : mysql_client_{} {}

ProblemRepository::ProblemRepository(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

std::vector<oj::common::ProblemSummary> ProblemRepository::list() const {
    std::vector<oj::common::ProblemSummary> items;
    for (const auto& meta : list_problem_meta()) {
        items.push_back({
            std::to_string(meta.id),
            meta.title,
            infer_difficulty(meta),
        });
    }
    return items;
}

std::vector<oj::protocol::ProblemMeta> ProblemRepository::list_problem_meta() const {
    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT id, title, time_limit_ms, memory_limit_mb "
            "FROM problems ORDER BY id ASC")};
    auto rows = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

    std::vector<oj::protocol::ProblemMeta> result;
    while (rows->next()) {
        oj::protocol::ProblemMeta meta;
        meta.id = rows->getInt64("id");
        meta.title = rows->getString("title");
        meta.time_limit_ms = rows->getInt("time_limit_ms");
        meta.memory_limit_mb = rows->getInt("memory_limit_mb");
        meta.tags = load_problem_tags(*connection, meta.id);
        result.push_back(std::move(meta));
    }
    return result;
}

std::optional<oj::protocol::ProblemDetail> ProblemRepository::find_detail(std::int64_t problem_id) const {
    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT p.id, p.title, p.time_limit_ms, p.memory_limit_mb, s.statement_markdown "
            "FROM problems p "
            "LEFT JOIN problem_statements s ON s.problem_id = p.id AND s.language = ? "
            "WHERE p.id = ?")};
    statement->setString(1, "zh-CN");
    statement->setInt64(2, problem_id);
    auto row = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!row->next()) {
        return std::nullopt;
    }

    oj::protocol::ProblemDetail detail;
    detail.id = row->getInt64("id");
    detail.title = row->getString("title");
    detail.time_limit_ms = row->getInt("time_limit_ms");
    detail.memory_limit_mb = row->getInt("memory_limit_mb");
    detail.statement = row->isNull("statement_markdown")
                           ? std::string{}
                           : static_cast<std::string>(row->getString("statement_markdown"));
    detail.tags = load_problem_tags(*connection, problem_id);

    const auto test_cases = load_test_cases(problem_id);
    for (std::size_t i = 0; i < test_cases.size() && i < 2; ++i) {
        detail.samples.push_back({test_cases[i].input, test_cases[i].expected_output});
    }

    return detail;
}

std::vector<oj::protocol::TestCase> ProblemRepository::load_test_cases(std::int64_t problem_id) const {
    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT input_data, expected_output "
            "FROM problem_testcases WHERE problem_id = ? ORDER BY case_no ASC")};
    statement->setInt64(1, problem_id);
    auto rows = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

    std::vector<oj::protocol::TestCase> test_cases;
    while (rows->next()) {
        oj::protocol::TestCase test_case;
        test_case.input = rows->getString("input_data");
        test_case.expected_output = rows->getString("expected_output");
        test_cases.push_back(std::move(test_case));
    }
    return test_cases;
}

} // namespace oj::server

