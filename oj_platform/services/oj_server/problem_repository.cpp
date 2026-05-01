#include "services/oj_server/problem_repository.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
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

bool problem_exists(sql::Connection& connection, std::int64_t problem_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement("SELECT id FROM problems WHERE id = ?")
    };
    statement->setInt64(1, problem_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    return result->next();
}

} // namespace

ProblemRepository::ProblemRepository()
    : mysql_client_{} {}

ProblemRepository::ProblemRepository(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

// 安全地修改题号，并保证旧题号存在且新题号不会与已有题目冲突。
void ProblemRepository::update_problem_id(std::int64_t old_problem_id, std::int64_t new_problem_id) const {
    if (old_problem_id <= 0 || new_problem_id <= 0) {
        throw std::runtime_error("problem id must be positive");
    }
    if (old_problem_id == new_problem_id) {
        return;
    }

    auto connection = mysql_client_.create_connection();
    if (!problem_exists(*connection, old_problem_id)) {
        throw std::runtime_error("problem not found");
    }
    if (problem_exists(*connection, new_problem_id)) {
        throw std::runtime_error("new problem id already exists");
    }

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement("UPDATE problems SET id = ?, updated_at = ? WHERE id = ?")
    };
    statement->setInt64(1, new_problem_id);
    statement->setInt64(2, std::time(nullptr));
    statement->setInt64(3, old_problem_id);
    statement->executeUpdate();
}

// 删除题目主记录，依赖数据库级联约束一并清理相关附属数据。
void ProblemRepository::delete_problem(std::int64_t problem_id) const {
    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement("DELETE FROM problems WHERE id = ?")
    };
    statement->setInt64(1, problem_id);
    if (statement->executeUpdate() == 0) {
        throw std::runtime_error("problem not found");
    }
}

// 更新题目标题并同步刷新更新时间，供后台编辑页即时生效。
void ProblemRepository::update_problem_title(std::int64_t problem_id, const std::string& title) const {
    if (title.empty()) {
        throw std::runtime_error("title cannot be empty");
    }

    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement("UPDATE problems SET title = ?, updated_at = ? WHERE id = ?")
    };
    statement->setString(1, title);
    statement->setInt64(2, std::time(nullptr));
    statement->setInt64(3, problem_id);
    if (statement->executeUpdate() == 0) {
        throw std::runtime_error("problem not found");
    }
}

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

// 从题目主表与标签表聚合出题目列表元信息，供首页列表与缓存使用。
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

// 加载单题完整展示信息，包括题面、限制和标签。
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
    detail.statement_markdown = row->isNull("statement_markdown")
                                    ? std::string{}
                                    : static_cast<std::string>(row->getString("statement_markdown"));
    detail.tags = load_problem_tags(*connection, problem_id);

    return detail;
}

// 按测试点编号顺序从数据库恢复判题所需的输入输出数据。
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

// 查询指定语言版本的题面文本，供后台编辑和前台展示复用。
std::optional<std::string> ProblemRepository::find_statement_markdown(
    std::int64_t problem_id,
    const std::string& language) const {
    auto connection = mysql_client_.create_connection();

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT statement_markdown "
            "FROM problem_statements "
            "WHERE problem_id = ? AND language = ?")
    };

    statement->setInt64(1, problem_id);
    statement->setString(2, language);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next()) {
        return std::nullopt;
    }

    if (result->isNull("statement_markdown")) {
        return std::string{};
    }

    return static_cast<std::string>(result->getString("statement_markdown"));
}

// 以 UPSERT 方式更新题面内容，避免后台编辑时拆成新增和修改两套流程。
void ProblemRepository::update_statement_markdown(
    std::int64_t problem_id,
    const std::string& language,
    const std::string& statement_markdown) const {
    auto connection = mysql_client_.create_connection();

    if (!problem_exists(*connection, problem_id)) {
        throw std::runtime_error("problem not found");
    }

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "INSERT INTO problem_statements "
            "(problem_id, language, statement_markdown) "
            "VALUES (?, ?, ?) "
            "ON DUPLICATE KEY UPDATE "
            "statement_markdown = VALUES(statement_markdown)")
    };

    statement->setInt64(1, problem_id);
    statement->setString(2, language);
    statement->setString(3, statement_markdown);
    statement->executeUpdate();
}

// 从给定起始题号开始寻找第一个未被占用的可用题号。
std::int64_t ProblemRepository::allocate_problem_id(std::int64_t start_id) const {
    auto connection = mysql_client_.create_connection();

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT id FROM problems WHERE id >= ? ORDER BY id ASC")
    };

    statement->setInt64(1, start_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

    std::int64_t candidate = start_id;
    while (result->next()) {
        const auto existing_id = result->getInt64("id");
        if (existing_id > candidate) {
            break;
        }
        if (existing_id == candidate) {
            ++candidate;
        }
    }

    return candidate;
}

// 在一个事务中导入题目主信息、题面、标签和测试点，保证导入结果一致。
void ProblemRepository::import_problem(const ImportedProblem& problem) const {
    auto connection = mysql_client_.create_connection();
    try {
        connection->setAutoCommit(false);

        {
            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement(
                    "INSERT INTO problems "
                    "(id, title, time_limit_ms, memory_limit_mb, checker_type, created_at, updated_at) "
                    "VALUES (?, ?, ?, ?, ?, UNIX_TIMESTAMP(), UNIX_TIMESTAMP())")
            };

            statement->setInt64(1, problem.id);
            statement->setString(2, problem.title);
            statement->setInt(3, problem.time_limit_ms);
            statement->setInt(4, problem.memory_limit_mb);
            statement->setString(5, problem.checker_type);
            statement->executeUpdate();
        }

        {
            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement(
                    "INSERT INTO problem_statements "
                    "(problem_id, language, statement_markdown) "
                    "VALUES (?, 'zh-CN', ?)")
            };

            statement->setInt64(1, problem.id);
            statement->setString(2, problem.statement_markdown);
            statement->executeUpdate();
        }

        for (const auto& tag : problem.tags) {
            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement(
                    "INSERT INTO problem_tags (problem_id, tag) VALUES (?, ?)")
            };
            statement->setInt64(1, problem.id);
            statement->setString(2, tag);
            statement->executeUpdate();
        }
        for (const auto& tc : problem.testcases) {
            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement(
                    "INSERT INTO problem_testcases "
                    "(problem_id, case_no, input_data, expected_output, is_sample) "
                    "VALUES (?, ?, ?, ?, ?)")
            };

            statement->setInt64(1, problem.id);
            statement->setInt(2, tc.case_no);
            statement->setString(3, tc.input_data);
            statement->setString(4, tc.expected_output);
            statement->setBoolean(5, tc.is_sample);
            statement->executeUpdate();
        }

        connection->commit();
        connection->setAutoCommit(true);
    } catch (...) {
        try {
            connection->rollback();
            connection->setAutoCommit(true);
        } catch (...) {
        }
        throw;
    }
}

} // namespace oj::server
