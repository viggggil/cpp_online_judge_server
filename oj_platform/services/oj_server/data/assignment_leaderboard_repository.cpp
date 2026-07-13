#include "services/oj_server/data/assignment_leaderboard_repository.h"

#include "common/platform_config.h"
#include "services/oj_server/data/redis_client.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oj::server {

namespace {

std::string assignment_rank_zset_key(std::int64_t assignment_id) {
    return "oj:assignment:" + std::to_string(assignment_id) + ":rank";
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

struct RankRow {
    std::int64_t user_id{0};
    std::string username;
    std::int64_t solved_count{0};
    std::int64_t score{0};
    std::int64_t penalty_seconds{0};
    std::int64_t rank_score{0};
};

struct ProblemStatsRow {
    std::int64_t user_id{0};
    std::int64_t problem_id{0};
    std::string username;
    std::int64_t submission_count{0};
    bool accepted{false};
    std::int64_t first_accepted_at{0};
    std::int64_t last_submitted_at{0};
    std::string last_status{"UNKNOWN"};
    std::int64_t score{0};
    std::int64_t penalty_seconds{0};
};

std::vector<RankRow> load_rank_rows_from_mysql(
    sql::Connection& connection,
    std::int64_t assignment_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT user_id, username_snapshot, solved_count, score, penalty_seconds, rank_score "
            "FROM assignment_user_rank_stats "
            "WHERE assignment_id = ? "
            "ORDER BY rank_score DESC, user_id ASC")
    };
    statement->setInt64(1, assignment_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    std::vector<RankRow> rows;
    while (result->next()) {
        RankRow row;
        row.user_id = result->getInt64("user_id");
        row.username = result->getString("username_snapshot");
        row.solved_count = result->getInt64("solved_count");
        row.score = result->getInt64("score");
        row.penalty_seconds = result->getInt64("penalty_seconds");
        row.rank_score = result->getInt64("rank_score");
        rows.push_back(std::move(row));
    }
    return rows;
}

std::unordered_map<std::int64_t, RankRow> load_rank_row_map(
    sql::Connection& connection,
    std::int64_t assignment_id) {
    auto rows = load_rank_rows_from_mysql(connection, assignment_id);
    std::unordered_map<std::int64_t, RankRow> by_user;
    by_user.reserve(rows.size());
    for (auto& row : rows) {
        by_user.emplace(row.user_id, std::move(row));
    }
    return by_user;
}

std::vector<ProblemStatsRow> load_problem_stats_rows(
    sql::Connection& connection,
    std::int64_t assignment_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT user_id, problem_id, username_snapshot, submission_count, accepted, "
            "first_accepted_at, last_submitted_at, last_status, score, penalty_seconds "
            "FROM assignment_user_problem_stats "
            "WHERE assignment_id = ?")
    };
    statement->setInt64(1, assignment_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    std::vector<ProblemStatsRow> rows;
    while (result->next()) {
        ProblemStatsRow row;
        row.user_id = result->getInt64("user_id");
        row.problem_id = result->getInt64("problem_id");
        row.username = result->getString("username_snapshot");
        row.submission_count = result->getInt64("submission_count");
        row.accepted = !result->isNull("accepted") && result->getBoolean("accepted");
        row.first_accepted_at =
            result->isNull("first_accepted_at") ? 0 : result->getInt64("first_accepted_at");
        row.last_submitted_at =
            result->isNull("last_submitted_at") ? 0 : result->getInt64("last_submitted_at");
        row.last_status = result->getString("last_status");
        row.score = result->getInt64("score");
        row.penalty_seconds = result->getInt64("penalty_seconds");
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<RankRow> load_rank_rows_from_redis_then_mysql(
    sql::Connection& connection,
    std::int64_t assignment_id,
    RedisClient& redis_client,
    long long ttl_seconds,
    bool* used_redis) {
    const auto key = assignment_rank_zset_key(assignment_id);
    const bool redis_has_key = redis_client.exists(key);

    if (used_redis != nullptr) {
        *used_redis = false;
    }

    if (!redis_has_key) {
        auto rows = load_rank_rows_from_mysql(connection, assignment_id);
        for (const auto& row : rows) {
            redis_client.zadd(key, static_cast<double>(row.rank_score), std::to_string(row.user_id));
        }
        if (!rows.empty()) {
            redis_client.expire(key, ttl_seconds);
        }
        return rows;
    }

    auto redis_rows = redis_client.zrevrange_with_scores(key, 0, -1);
    if (redis_rows.empty()) {
        auto rows = load_rank_rows_from_mysql(connection, assignment_id);
        for (const auto& row : rows) {
            redis_client.zadd(key, static_cast<double>(row.rank_score), std::to_string(row.user_id));
        }
        if (!rows.empty()) {
            redis_client.expire(key, ttl_seconds);
        }
        return rows;
    }

    auto rank_map = load_rank_row_map(connection, assignment_id);
    std::vector<RankRow> rows;
    rows.reserve(redis_rows.size());

    for (const auto& item : redis_rows) {
        std::int64_t user_id = 0;
        try {
            user_id = std::stoll(item.first);
        } catch (...) {
            continue;
        }
        const auto iter = rank_map.find(user_id);
        if (iter == rank_map.end()) {
            continue;
        }
        rows.push_back(iter->second);
    }

    if (rows.size() != rank_map.size()) {
        auto mysql_rows = load_rank_rows_from_mysql(connection, assignment_id);
        for (const auto& row : mysql_rows) {
            redis_client.zadd(key, static_cast<double>(row.rank_score), std::to_string(row.user_id));
        }
        if (!mysql_rows.empty()) {
            redis_client.expire(key, ttl_seconds);
        }
        return mysql_rows;
    }

    if (used_redis != nullptr) {
        *used_redis = true;
    }
    return rows;
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

    const oj::common::RedisConfig redis_config{};
    RedisClient redis_client{redis_config};
    bool used_redis = false;
    auto rank_rows = load_rank_rows_from_redis_then_mysql(
        *connection,
        assignment_id,
        redis_client,
        redis_config.assignment_leaderboard_ttl_seconds,
        &used_redis);

    auto problem_rows = load_problem_stats_rows(*connection, assignment_id);

    std::vector<oj::common::AssignmentLeaderboardEntry> entries;
    entries.reserve(rank_rows.size());
    std::unordered_map<std::int64_t, std::size_t> entry_index_by_user;
    entry_index_by_user.reserve(rank_rows.size());

    int displayed_rank = 0;
    int position = 0;
    std::optional<std::int64_t> previous_rank_score;

    for (const auto& rank_row : rank_rows) {
        oj::common::AssignmentLeaderboardEntry entry;
        entry.username = rank_row.username;
        ++position;

        if (!previous_rank_score || *previous_rank_score != rank_row.rank_score) {
            displayed_rank = position;
            previous_rank_score = rank_row.rank_score;
        }

        entry.rank = displayed_rank;
        entry.solved_count = rank_row.solved_count;
        entry.score = rank_row.score;
        entry.penalty_seconds = rank_row.penalty_seconds;
        initialize_entry_cells(entry, leaderboard.problems);
        entry_index_by_user.emplace(rank_row.user_id, entries.size());
        entries.push_back(std::move(entry));
    }

    for (const auto& row : problem_rows) {
        const auto problem_iter = problem_index_by_id.find(row.problem_id);
        if (problem_iter == problem_index_by_id.end()) {
            continue;
        }

        auto& column = leaderboard.problems.at(problem_iter->second);

        column.submission_count += row.submission_count;
        if (row.accepted) {
            column.accepted_user_count += 1;
        }

        const auto entry_index_iter = entry_index_by_user.find(row.user_id);
        if (entry_index_iter == entry_index_by_user.end()) {
            continue;
        }

        auto& entry = entries.at(entry_index_iter->second);
        auto& cell = entry.cells.at(problem_iter->second);

        cell.problem_id = row.problem_id;
        cell.alias = column.alias;
        cell.has_submission = row.submission_count > 0;
        cell.accepted = row.accepted;
        cell.score = static_cast<int>(row.score);
        cell.status = row.accepted ? "ACCEPTED" : row.last_status;
        cell.time_from_start_seconds =
            row.accepted
                ? row.penalty_seconds
                : std::max<std::int64_t>(0, row.last_submitted_at - leaderboard.start_at);
        cell.first_accepted_at = row.first_accepted_at;
        cell.last_submitted_at = row.last_submitted_at;
        cell.submission_count = row.submission_count;
    }

    leaderboard.entries = std::move(entries);

    return leaderboard;
}

} // namespace oj::server
