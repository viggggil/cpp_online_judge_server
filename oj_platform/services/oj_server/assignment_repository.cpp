#include "services/oj_server/assignment_repository.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

namespace oj::server {
namespace {

std::int64_t now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string default_alias_for_index(std::size_t index) {
    if (index < 26) {
        return std::string(1, static_cast<char>('A' + index));
    }

    return "P" + std::to_string(index + 1);
}

void validate_create_assignment_request(const CreateAssignmentRequest& request) {
    if (request.title.empty()) {
        throw std::runtime_error("assignment title cannot be empty");
    }

    if (request.title.size() > 255) {
        throw std::runtime_error("assignment title is too long");
    }

    if (request.description_markdown.size() > 1024 * 1024) {
        throw std::runtime_error("assignment description is too large");
    }

    if (request.start_at <= 0) {
        throw std::runtime_error("assignment start_at must be positive");
    }

    if (request.end_at <= request.start_at) {
        throw std::runtime_error("assignment end_at must be greater than start_at");
    }

    if (request.created_by <= 0) {
        throw std::runtime_error("assignment created_by must be positive");
    }

    if (request.problems.empty()) {
        throw std::runtime_error("assignment must contain at least one problem");
    }

    std::set<std::int64_t> problem_ids;
    std::set<std::string> aliases;

    for (const auto& problem : request.problems) {
        if (problem.problem_id <= 0) {
            throw std::runtime_error("assignment problem_id must be positive");
        }

        if (!problem_ids.insert(problem.problem_id).second) {
            throw std::runtime_error(
                "duplicated problem_id in assignment: " +
                std::to_string(problem.problem_id));
        }

        if (problem.alias.size() > 32) {
            throw std::runtime_error("assignment problem alias is too long");
        }

        if (!problem.alias.empty() && !aliases.insert(problem.alias).second) {
            throw std::runtime_error(
                "duplicated assignment problem alias: " + problem.alias);
        }
    }
}

bool user_exists(sql::Connection& connection, std::int64_t user_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement("SELECT id FROM users WHERE id = ?")
    };

    statement->setInt64(1, user_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    return result->next();
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

AssignmentRepository::AssignmentRepository() : mysql_client_{} {}

AssignmentRepository::AssignmentRepository(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

std::int64_t AssignmentRepository::create_assignment(
    const CreateAssignmentRequest& request) const {
    validate_create_assignment_request(request);

    auto connection = mysql_client_.create_connection();

    try {
        connection->setAutoCommit(false);

        if (!user_exists(*connection, request.created_by)) {
            throw std::runtime_error("assignment creator user not found");
        }

        for (const auto& problem : request.problems) {
            if (!problem_exists(*connection, problem.problem_id)) {
                throw std::runtime_error(
                    "assignment problem not found: " +
                    std::to_string(problem.problem_id));
            }
        }

        const auto now = now_unix_seconds();
        std::int64_t assignment_id = 0;

        {
            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement(
                    "INSERT INTO assignments "
                    "(title, description_markdown, start_at, end_at, "
                    "created_by, created_at, updated_at) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?)")
            };

            statement->setString(1, request.title);
            statement->setString(2, request.description_markdown);
            statement->setInt64(3, request.start_at);
            statement->setInt64(4, request.end_at);
            statement->setInt64(5, request.created_by);
            statement->setInt64(6, now);
            statement->setInt64(7, now);
            statement->executeUpdate();
        }

        {
            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement("SELECT LAST_INSERT_ID() AS id")
            };

            auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
            if (!result->next()) {
                throw std::runtime_error("failed to fetch assignment id");
            }

            assignment_id = result->getInt64("id");
        }

        for (std::size_t i = 0; i < request.problems.size(); ++i) {
            const auto& problem = request.problems[i];

            const int display_order = static_cast<int>(i + 1);
            const std::string alias =
                problem.alias.empty() ? default_alias_for_index(i) : problem.alias;

            auto statement = std::unique_ptr<sql::PreparedStatement>{
                connection->prepareStatement(
                    "INSERT INTO assignment_problems "
                    "(assignment_id, problem_id, display_order, alias) "
                    "VALUES (?, ?, ?, ?)")
            };

            statement->setInt64(1, assignment_id);
            statement->setInt64(2, problem.problem_id);
            statement->setInt(3, display_order);
            statement->setString(4, alias);
            statement->executeUpdate();
        }

        connection->commit();
        connection->setAutoCommit(true);

        return assignment_id;
    } catch (...) {
        try {
            connection->rollback();
            connection->setAutoCommit(true);
        } catch (...) {
        }

        throw;
    }
}

std::vector<AssignmentSummary> AssignmentRepository::list_assignments() const {
    auto connection = mysql_client_.create_connection();

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT "
            "a.id, a.title, a.start_at, a.end_at, "
            "a.created_by, a.created_at, a.updated_at, "
            "COUNT(ap.id) AS problem_count "
            "FROM assignments a "
            "LEFT JOIN assignment_problems ap ON ap.assignment_id = a.id "
            "GROUP BY "
            "a.id, a.title, a.start_at, a.end_at, "
            "a.created_by, a.created_at, a.updated_at "
            "ORDER BY a.start_at DESC, a.id DESC")
    };

    auto rows = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

    std::vector<AssignmentSummary> assignments;
    while (rows->next()) {
        AssignmentSummary item;
        item.id = rows->getInt64("id");
        item.title = rows->getString("title");
        item.start_at = rows->getInt64("start_at");
        item.end_at = rows->getInt64("end_at");
        item.created_by = rows->getInt64("created_by");
        item.created_at = rows->getInt64("created_at");
        item.updated_at = rows->getInt64("updated_at");
        item.problem_count = rows->getInt("problem_count");

        assignments.push_back(std::move(item));
    }

    return assignments;
}

std::optional<AssignmentDetail> AssignmentRepository::find_assignment_detail(
    std::int64_t assignment_id) const {
    if (assignment_id <= 0) {
        throw std::runtime_error("assignment_id must be positive");
    }

    auto connection = mysql_client_.create_connection();

    AssignmentDetail detail;

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "SELECT "
                "id, title, description_markdown, start_at, end_at, "
                "created_by, created_at, updated_at "
                "FROM assignments "
                "WHERE id = ?")
        };

        statement->setInt64(1, assignment_id);

        auto row = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        if (!row->next()) {
            return std::nullopt;
        }

        detail.id = row->getInt64("id");
        detail.title = row->getString("title");
        detail.description_markdown = row->getString("description_markdown");
        detail.start_at = row->getInt64("start_at");
        detail.end_at = row->getInt64("end_at");
        detail.created_by = row->getInt64("created_by");
        detail.created_at = row->getInt64("created_at");
        detail.updated_at = row->getInt64("updated_at");
    }

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "SELECT "
                "ap.problem_id, p.title, ap.display_order, ap.alias "
                "FROM assignment_problems ap "
                "JOIN problems p ON p.id = ap.problem_id "
                "WHERE ap.assignment_id = ? "
                "ORDER BY ap.display_order ASC, ap.id ASC")
        };

        statement->setInt64(1, assignment_id);

        auto rows = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        while (rows->next()) {
            AssignmentProblemItem item;
            item.problem_id = rows->getInt64("problem_id");
            item.title = rows->getString("title");
            item.display_order = rows->getInt("display_order");
            item.alias = rows->getString("alias");

            detail.problems.push_back(std::move(item));
        }
    }

    return detail;
}

bool AssignmentRepository::assignment_exists(std::int64_t assignment_id) const {
    if (assignment_id <= 0) {
        return false;
    }

    auto connection = mysql_client_.create_connection();

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement("SELECT id FROM assignments WHERE id = ?")
    };

    statement->setInt64(1, assignment_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    return result->next();
}

bool AssignmentRepository::assignment_contains_problem(
    std::int64_t assignment_id,
    std::int64_t problem_id) const {
    if (assignment_id <= 0 || problem_id <= 0) {
        return false;
    }

    auto connection = mysql_client_.create_connection();

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT id "
            "FROM assignment_problems "
            "WHERE assignment_id = ? AND problem_id = ?")
    };

    statement->setInt64(1, assignment_id);
    statement->setInt64(2, problem_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    return result->next();
}

} // namespace oj::server
