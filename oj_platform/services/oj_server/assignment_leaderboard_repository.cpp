#include "services/oj_server/assignment_leaderboard_repository.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oj::server {

namespace {

std::int64_t seconds_from_start(std::int64_t timestamp, std::int64_t start_at) {
    if (timestamp <= 0 || start_at <= 0 || timestamp <= start_at) {
        return 0;
    }

    return timestamp - start_at;
}

void initialize_entry_cells(
    oj::common::AssignmentLeaderboardEntry& entry,
    const std::vector<oj::common::AssignmentLeaderboardProblemColumn>& problems) {
    entry.cells.clear();
    entry.cells.reserve(problems.size());

    for (const auto& problem : problems) {
        oj::common::AssignmentLeaderboardCell cell;
        cell.problem_id = problem.problem_id;
        cell.alias = problem.alias;
        entry.cells.push_back(std::move(cell));
    }
}

} // namespace

AssignmentLeaderboardRepository::AssignmentLeaderboardRepository()
    : mysql_client_{} {}

AssignmentLeaderboardRepository::AssignmentLeaderboardRepository(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

std::optional<oj::common::AssignmentLeaderboard>
AssignmentLeaderboardRepository::find_assignment_leaderboard(std::int64_t assignment_id) const {
    if (assignment_id <= 0) {
        throw std::runtime_error("assignment_id must be positive");
    }

    auto connection = mysql_client_.create_connection();

    oj::common::AssignmentLeaderboard leaderboard;

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "SELECT id, title, start_at, end_at "
                "FROM assignments "
                "WHERE id = ?")
        };

        statement->setInt64(1, assignment_id);

        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        if (!result->next()) {
            return std::nullopt;
        }

        leaderboard.assignment_id = result->getInt64("id");
        leaderboard.title = result->getString("title");
        leaderboard.start_at = result->getInt64("start_at");
        leaderboard.end_at = result->getInt64("end_at");
    }

    std::map<std::int64_t, std::size_t> problem_index_by_id;

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "SELECT "
                "ap.problem_id, "
                "ap.alias, "
                "ap.display_order, "
                "p.title "
                "FROM assignment_problems ap "
                "JOIN problems p ON p.id = ap.problem_id "
                "WHERE ap.assignment_id = ? "
                "ORDER BY ap.display_order ASC, ap.id ASC")
        };

        statement->setInt64(1, assignment_id);

        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

        while (result->next()) {
            oj::common::AssignmentLeaderboardProblemColumn column;
            column.problem_id = result->getInt64("problem_id");
            column.alias = result->getString("alias");
            column.title = result->getString("title");
            column.display_order = result->getInt("display_order");
            column.accepted_user_count = 0;
            column.submission_count = 0;

            problem_index_by_id[column.problem_id] = leaderboard.problems.size();
            leaderboard.problems.push_back(std::move(column));
        }
    }

    if (leaderboard.problems.empty()) {
        return leaderboard;
    }

    std::map<std::int64_t, oj::common::AssignmentLeaderboardEntry> entries_by_user;

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "SELECT "
                "s.user_id, "
                "u.username, "
                "s.problem_id, "
                "COUNT(*) AS submission_count, "
                "MIN(CASE WHEN s.accepted = 1 THEN s.created_at ELSE NULL END) AS first_accepted_at, "
                "MAX(s.created_at) AS last_submitted_at, "
                "SUBSTRING_INDEX("
                "  GROUP_CONCAT(s.final_status ORDER BY s.created_at DESC, s.id DESC SEPARATOR ','), "
                "  ',', "
                "  1"
                ") AS last_status "
                "FROM submissions s "
                "JOIN users u ON u.id = s.user_id "
                "JOIN assignments a ON a.id = s.assignment_id "
                "JOIN assignment_problems ap "
                "  ON ap.assignment_id = s.assignment_id "
                " AND ap.problem_id = s.problem_id "
                "WHERE s.assignment_id = ? "
                "  AND s.created_at >= a.start_at "
                "  AND s.created_at <= a.end_at "
                "  AND u.role <> 'admin' "
                "GROUP BY s.user_id, u.username, s.problem_id")
        };

        statement->setInt64(1, assignment_id);

        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

        while (result->next()) {
            const auto user_id = result->getInt64("user_id");
            const std::string username = result->getString("username");
            const auto problem_id = result->getInt64("problem_id");

            const auto problem_index_iter = problem_index_by_id.find(problem_id);
            if (problem_index_iter == problem_index_by_id.end()) {
                continue;
            }

            const auto problem_index = problem_index_iter->second;

            auto entry_iter = entries_by_user.find(user_id);
            if (entry_iter == entries_by_user.end()) {
                oj::common::AssignmentLeaderboardEntry entry;
                entry.username = username;
                entry.rank = 0;
                entry.solved_count = 0;
                entry.score = 0;
                entry.penalty_seconds = 0;

                initialize_entry_cells(entry, leaderboard.problems);

                entry_iter = entries_by_user.emplace(user_id, std::move(entry)).first;
            }

            auto& entry = entry_iter->second;
            auto& cell = entry.cells.at(problem_index);
            auto& column = leaderboard.problems.at(problem_index);

            const auto submission_count = result->getInt64("submission_count");
            const auto last_submitted_at = result->getInt64("last_submitted_at");
            const std::string last_status = result->getString("last_status");

            const bool has_accepted = !result->isNull("first_accepted_at");
            const auto first_accepted_at =
                has_accepted ? result->getInt64("first_accepted_at") : 0;

            cell.problem_id = problem_id;
            cell.alias = column.alias;
            cell.has_submission = true;
            cell.submission_count = submission_count;
            cell.last_submitted_at = last_submitted_at;

            column.submission_count += submission_count;

            if (has_accepted) {
                const auto accepted_seconds =
                    seconds_from_start(first_accepted_at, leaderboard.start_at);

                cell.accepted = true;
                cell.score = 100;
                cell.status = "ACCEPTED";
                cell.first_accepted_at = first_accepted_at;
                cell.time_from_start_seconds = accepted_seconds;

                entry.solved_count += 1;
                entry.score += 100;
                entry.penalty_seconds += accepted_seconds;

                column.accepted_user_count += 1;
            } else {
                cell.accepted = false;
                cell.score = 0;
                cell.status = last_status.empty() ? "UNKNOWN" : last_status;
                cell.first_accepted_at = 0;
                cell.time_from_start_seconds =
                    seconds_from_start(last_submitted_at, leaderboard.start_at);
            }
        }
    }

    leaderboard.entries.reserve(entries_by_user.size());
    for (auto& item : entries_by_user) {
        leaderboard.entries.push_back(std::move(item.second));
    }

    std::sort(
        leaderboard.entries.begin(),
        leaderboard.entries.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.solved_count != rhs.solved_count) {
                return lhs.solved_count > rhs.solved_count;
            }

            if (lhs.penalty_seconds != rhs.penalty_seconds) {
                return lhs.penalty_seconds < rhs.penalty_seconds;
            }

            return lhs.username < rhs.username;
        });

    int displayed_rank = 0;
    int position = 0;

    std::int64_t previous_solved_count = -1;
    std::int64_t previous_penalty_seconds = -1;

    for (auto& entry : leaderboard.entries) {
        ++position;

        const bool same_as_previous =
            previous_solved_count == entry.solved_count &&
            previous_penalty_seconds == entry.penalty_seconds;

        if (!same_as_previous) {
            displayed_rank = position;
            previous_solved_count = entry.solved_count;
            previous_penalty_seconds = entry.penalty_seconds;
        }

        entry.rank = displayed_rank;
    }

    return leaderboard;
}

} // namespace oj::server
