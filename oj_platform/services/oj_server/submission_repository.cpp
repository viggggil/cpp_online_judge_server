#include "services/oj_server/submission_repository.h"

#include "common/platform_config.h"
#include "services/oj_server/redis_client.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <stdexcept>
#include <utility>

namespace oj::server {

namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

constexpr std::int64_t kRankScoreSolvedWeight = 1000000000LL;

std::string assignment_rank_zset_key(std::int64_t assignment_id) {
    return "oj:assignment:" + std::to_string(assignment_id) + ":rank";
}

bool is_terminal_submission_status(const std::string& status) {
    return status == "OK" ||
           status == "COMPILE_ERROR" ||
           status == "RUNTIME_ERROR" ||
           status == "TIME_LIMIT_EXCEEDED" ||
           status == "MEMORY_LIMIT_EXCEEDED" ||
           status == "OUTPUT_LIMIT_EXCEEDED" ||
           status == "WRONG_ANSWER" ||
           status == "PRESENTATION_ERROR" ||
           status == "SYSTEM_ERROR" ||
           status == "NOT_FOUND";
}

std::int64_t seconds_from_start(std::int64_t timestamp, std::int64_t start_at) {
    if (timestamp <= 0 || start_at <= 0 || timestamp <= start_at) {
        return 0;
    }
    return timestamp - start_at;
}

std::int64_t encode_rank_score(std::int64_t solved_count, std::int64_t penalty_seconds) {
    return solved_count * kRankScoreSolvedWeight - penalty_seconds;
}

bool has_assignment_id_column(sql::Connection& connection) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT COUNT(*) AS cnt "
            "FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() "
            "  AND TABLE_NAME = 'submissions' "
            "  AND COLUMN_NAME = 'assignment_id'")
    };
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    return result->next() && result->getInt("cnt") > 0;
}

oj::protocol::JudgeStatus parse_judge_status(const std::string& text) {
    if (text == "OK") return oj::protocol::JudgeStatus::ok;
    if (text == "COMPILE_ERROR") return oj::protocol::JudgeStatus::compile_error;
    if (text == "RUNTIME_ERROR") return oj::protocol::JudgeStatus::runtime_error;
    if (text == "TIME_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::time_limit_exceeded;
    if (text == "MEMORY_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::memory_limit_exceeded;
    if (text == "OUTPUT_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::output_limit_exceeded;
    if (text == "WRONG_ANSWER") return oj::protocol::JudgeStatus::wrong_answer;
    if (text == "PRESENTATION_ERROR") return oj::protocol::JudgeStatus::presentation_error;
    return oj::protocol::JudgeStatus::system_error;
}

std::optional<std::int64_t> find_db_id(sql::Connection& connection, const std::string& submission_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement("SELECT id FROM submissions WHERE submission_id = ?")};
    statement->setString(1, submission_id);
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next()) {
        return std::nullopt;
    }
    return result->getInt64("id");
}

std::optional<std::int64_t> find_user_id(sql::Connection& connection, const std::string& username) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement("SELECT id FROM users WHERE username = ?")};
    statement->setString(1, username);
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next()) {
        return std::nullopt;
    }
    return result->getInt64("id");
}

// 将 submissions 主表中的一行记录恢复成服务层统一使用的提交对象。
StoredSubmission build_submission_from_row(sql::ResultSet& row) {
    StoredSubmission stored;
    stored.result.submission_id = row.getString("submission_id");
    stored.result.username = row.getString("username_snapshot");
    stored.result.problem_id = row.getString("problem_id_text");
    stored.result.language = row.getString("language");
    stored.result.source_code = row.getString("source_code");
    stored.result.status = row.getString("status");
    stored.result.detail = row.getString("detail");
    stored.result.accepted = row.getBoolean("accepted");
    stored.username = row.getString("username_snapshot");
    stored.created_at = row.getInt64("created_at");
    stored.updated_at = row.getInt64("updated_at");

    stored.result.judge_response.final_status = parse_judge_status(row.getString("final_status"));
    stored.result.judge_response.compile_success = row.getBoolean("compile_success");
    stored.result.judge_response.compile_stdout = row.getString("compile_stdout");
    stored.result.judge_response.compile_stderr = row.getString("compile_stderr");
    stored.result.judge_response.total_time_used_ms = row.getInt("total_time_used_ms");
    stored.result.judge_response.peak_memory_used_kb = row.getInt("peak_memory_used_kb");
    stored.result.judge_response.system_message = row.getString("system_message");
    return stored;
}

// 把 submission_testcases 子表中的逐点评测结果追加到完整提交对象里。
void load_testcases(sql::Connection& connection, std::int64_t submission_db_id, StoredSubmission& stored) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT case_no, status, time_used_ms, memory_used_kb, input_data, expected_output, actual_output, error_message "
            "FROM submission_testcases WHERE submission_db_id = ? ORDER BY case_no ASC")};
    statement->setInt64(1, submission_db_id);
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    while (result->next()) {
        oj::protocol::TestCaseResult item;
        item.status = parse_judge_status(result->getString("status"));
        item.time_used_ms = result->getInt("time_used_ms");
        item.memory_used_kb = result->getInt("memory_used_kb");
        item.input = result->getString("input_data");
        item.expected_output = result->getString("expected_output");
        item.actual_output = result->getString("actual_output");
        item.error_message = result->getString("error_message");
        stored.result.judge_response.test_case_results.push_back(std::move(item));
    }
}

struct SubmissionAssignmentContext {
    std::int64_t submission_db_id{0};
    std::int64_t assignment_id{0};
    std::int64_t user_id{0};
    std::int64_t problem_id{0};
    std::string username_snapshot;
    std::string final_status;
    bool accepted{false};
    std::int64_t created_at{0};
    std::int64_t assignment_start_at{0};
    std::int64_t assignment_end_at{0};
};

struct ProblemStatsSnapshot {
    std::int64_t submission_count{0};
    bool accepted{false};
    std::int64_t first_accepted_at{0};
    std::int64_t last_submitted_at{0};
    std::string last_status{"UNKNOWN"};
    std::int64_t score{0};
    std::int64_t penalty_seconds{0};
    std::int64_t last_processed_submission_id{0};
};

struct RankStatsSnapshot {
    std::int64_t solved_count{0};
    std::int64_t score{0};
    std::int64_t penalty_seconds{0};
    std::int64_t rank_score{0};
    std::int64_t last_processed_submission_id{0};
};

std::optional<SubmissionAssignmentContext> load_submission_assignment_context(
    sql::Connection& connection,
    std::int64_t submission_db_id) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT "
            "s.id, "
            "s.assignment_id, "
            "s.user_id, "
            "s.problem_id, "
            "s.username_snapshot, "
            "s.final_status, "
            "s.accepted, "
            "s.created_at, "
            "a.start_at, "
            "a.end_at "
            "FROM submissions s "
            "LEFT JOIN assignments a ON a.id = s.assignment_id "
            "WHERE s.id = ? "
            "FOR UPDATE")
    };
    statement->setInt64(1, submission_db_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next() || result->isNull("assignment_id")) {
        return std::nullopt;
    }

    SubmissionAssignmentContext context;
    context.submission_db_id = result->getInt64("id");
    context.assignment_id = result->getInt64("assignment_id");
    context.user_id = result->getInt64("user_id");
    context.problem_id = result->getInt64("problem_id");
    context.username_snapshot = result->getString("username_snapshot");
    context.final_status = result->getString("final_status");
    context.accepted = result->getBoolean("accepted");
    context.created_at = result->getInt64("created_at");
    context.assignment_start_at =
        result->isNull("start_at") ? 0 : result->getInt64("start_at");
    context.assignment_end_at =
        result->isNull("end_at") ? 0 : result->getInt64("end_at");
    return context;
}

bool should_count_for_assignment_leaderboard(
    const SubmissionAssignmentContext& context) {
    if (context.assignment_id <= 0) {
        return false;
    }
    if (!is_terminal_submission_status(context.final_status)) {
        return false;
    }
    if (context.created_at < context.assignment_start_at) {
        return false;
    }
    if (context.assignment_end_at > 0 && context.created_at > context.assignment_end_at) {
        return false;
    }
    return true;
}

ProblemStatsSnapshot build_problem_stats_snapshot(
    sql::Connection& connection,
    const SubmissionAssignmentContext& context) {
    ProblemStatsSnapshot snapshot;

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "SELECT "
                "COUNT(*) AS submission_count, "
                "MIN(CASE WHEN s.accepted = 1 THEN s.created_at ELSE NULL END) AS first_accepted_at, "
                "MAX(s.created_at) AS last_submitted_at, "
                "MAX(CASE WHEN s.accepted = 1 THEN 1 ELSE 0 END) AS accepted, "
                "MAX(s.id) AS last_processed_submission_id "
                "FROM submissions s "
                "JOIN assignment_problems ap "
                "  ON ap.assignment_id = s.assignment_id "
                " AND ap.problem_id = s.problem_id "
                "WHERE s.assignment_id = ? "
                "  AND s.user_id = ? "
                "  AND s.problem_id = ? "
                "  AND s.created_at >= ? "
                "  AND s.created_at <= ? "
                "  AND s.final_status NOT IN ('QUEUED', 'RUNNING')")
        };
        statement->setInt64(1, context.assignment_id);
        statement->setInt64(2, context.user_id);
        statement->setInt64(3, context.problem_id);
        statement->setInt64(4, context.assignment_start_at);
        statement->setInt64(5, context.assignment_end_at);

        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        if (result->next()) {
            snapshot.submission_count = result->getInt64("submission_count");
            snapshot.accepted = result->getBoolean("accepted");
            snapshot.first_accepted_at =
                result->isNull("first_accepted_at") ? 0 : result->getInt64("first_accepted_at");
            snapshot.last_submitted_at =
                result->isNull("last_submitted_at") ? 0 : result->getInt64("last_submitted_at");
            snapshot.last_processed_submission_id =
                result->isNull("last_processed_submission_id")
                    ? 0
                    : result->getInt64("last_processed_submission_id");
        }
    }

    {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "SELECT s.final_status "
                "FROM submissions s "
                "JOIN assignment_problems ap "
                "  ON ap.assignment_id = s.assignment_id "
                " AND ap.problem_id = s.problem_id "
                "WHERE s.assignment_id = ? "
                "  AND s.user_id = ? "
                "  AND s.problem_id = ? "
                "  AND s.created_at >= ? "
                "  AND s.created_at <= ? "
                "  AND s.final_status NOT IN ('QUEUED', 'RUNNING') "
                "ORDER BY s.created_at DESC, s.id DESC "
                "LIMIT 1")
        };
        statement->setInt64(1, context.assignment_id);
        statement->setInt64(2, context.user_id);
        statement->setInt64(3, context.problem_id);
        statement->setInt64(4, context.assignment_start_at);
        statement->setInt64(5, context.assignment_end_at);

        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        if (result->next()) {
            snapshot.last_status = result->getString("final_status");
        }
    }

    if (snapshot.accepted) {
        snapshot.score = 100;
        snapshot.penalty_seconds =
            seconds_from_start(snapshot.first_accepted_at, context.assignment_start_at);
    }

    return snapshot;
}

void upsert_problem_stats_snapshot(
    sql::Connection& connection,
    const SubmissionAssignmentContext& context,
    const ProblemStatsSnapshot& snapshot,
    std::int64_t now) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "INSERT INTO assignment_user_problem_stats "
            "(assignment_id, user_id, problem_id, username_snapshot, submission_count, accepted, "
            "first_accepted_at, last_submitted_at, last_status, score, penalty_seconds, "
            "last_processed_submission_id, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE "
            "username_snapshot = VALUES(username_snapshot), "
            "submission_count = VALUES(submission_count), "
            "accepted = VALUES(accepted), "
            "first_accepted_at = VALUES(first_accepted_at), "
            "last_submitted_at = VALUES(last_submitted_at), "
            "last_status = VALUES(last_status), "
            "score = VALUES(score), "
            "penalty_seconds = VALUES(penalty_seconds), "
            "last_processed_submission_id = VALUES(last_processed_submission_id), "
            "updated_at = VALUES(updated_at)")
    };
    statement->setInt64(1, context.assignment_id);
    statement->setInt64(2, context.user_id);
    statement->setInt64(3, context.problem_id);
    statement->setString(4, context.username_snapshot);
    statement->setInt64(5, snapshot.submission_count);
    statement->setBoolean(6, snapshot.accepted);
    statement->setInt64(7, snapshot.first_accepted_at);
    statement->setInt64(8, snapshot.last_submitted_at);
    statement->setString(9, snapshot.last_status);
    statement->setInt64(10, snapshot.score);
    statement->setInt64(11, snapshot.penalty_seconds);
    statement->setInt64(12, snapshot.last_processed_submission_id);
    statement->setInt64(13, now);
    statement->executeUpdate();
}

RankStatsSnapshot build_rank_stats_snapshot(
    sql::Connection& connection,
    std::int64_t assignment_id,
    std::int64_t user_id) {
    RankStatsSnapshot snapshot;

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT "
            "COALESCE(SUM(accepted), 0) AS solved_count, "
            "COALESCE(SUM(score), 0) AS score, "
            "COALESCE(SUM(penalty_seconds), 0) AS penalty_seconds, "
            "COALESCE(MAX(last_processed_submission_id), 0) AS last_processed_submission_id "
            "FROM assignment_user_problem_stats "
            "WHERE assignment_id = ? AND user_id = ?")
    };
    statement->setInt64(1, assignment_id);
    statement->setInt64(2, user_id);

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (result->next()) {
        snapshot.solved_count = result->getInt64("solved_count");
        snapshot.score = result->getInt64("score");
        snapshot.penalty_seconds = result->getInt64("penalty_seconds");
        snapshot.last_processed_submission_id = result->getInt64("last_processed_submission_id");
        snapshot.rank_score =
            encode_rank_score(snapshot.solved_count, snapshot.penalty_seconds);
    }

    return snapshot;
}

void upsert_rank_stats_snapshot(
    sql::Connection& connection,
    const SubmissionAssignmentContext& context,
    const RankStatsSnapshot& snapshot,
    std::int64_t now) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "INSERT INTO assignment_user_rank_stats "
            "(assignment_id, user_id, username_snapshot, solved_count, score, penalty_seconds, "
            "rank_score, last_processed_submission_id, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE "
            "username_snapshot = VALUES(username_snapshot), "
            "solved_count = VALUES(solved_count), "
            "score = VALUES(score), "
            "penalty_seconds = VALUES(penalty_seconds), "
            "rank_score = VALUES(rank_score), "
            "last_processed_submission_id = VALUES(last_processed_submission_id), "
            "updated_at = VALUES(updated_at)")
    };
    statement->setInt64(1, context.assignment_id);
    statement->setInt64(2, context.user_id);
    statement->setString(3, context.username_snapshot);
    statement->setInt64(4, snapshot.solved_count);
    statement->setInt64(5, snapshot.score);
    statement->setInt64(6, snapshot.penalty_seconds);
    statement->setInt64(7, snapshot.rank_score);
    statement->setInt64(8, snapshot.last_processed_submission_id);
    statement->setInt64(9, now);
    statement->executeUpdate();
}

} // namespace

SubmissionRepository::SubmissionRepository()
    : mysql_client_{} {}

SubmissionRepository::SubmissionRepository(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

// 在用户提交代码时先创建一条初始记录，为异步判题回填结果预留主键。
void SubmissionRepository::create_submission(const std::string& submission_id,
                                             const std::string& username,
                                             const oj::common::SubmissionRequest& request,
                                             const std::string& status,
                                             const std::string& detail) const {
    auto connection = mysql_client_.create_connection();
    const auto user_id = find_user_id(*connection, username);
    if (!user_id) {
        throw std::runtime_error("user not found for submission: " + username);
    }

    const auto now = unix_now();
    const auto with_assignment_id = has_assignment_id_column(*connection);

    if (with_assignment_id) {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "INSERT INTO submissions "
                "(submission_id, user_id, username_snapshot, problem_id, problem_id_text, assignment_id, language, source_code, status, final_status, accepted, detail, compile_success, compile_stdout, compile_stderr, total_time_used_ms, peak_memory_used_kb, system_message, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")};
        statement->setString(1, submission_id);
        statement->setInt64(2, *user_id);
        statement->setString(3, username);
        statement->setInt64(4, std::stoll(request.problem_id));
        statement->setString(5, request.problem_id);

        if (request.assignment_id) {
            statement->setInt64(6, *request.assignment_id);
        } else {
            statement->setNull(6, sql::DataType::BIGINT);
        }

        statement->setString(7, request.language);
        statement->setString(8, request.source_code);
        statement->setString(9, status);
        statement->setString(10, status);
        statement->setBoolean(11, false);
        statement->setString(12, detail);
        statement->setBoolean(13, false);
        statement->setString(14, "");
        statement->setString(15, "");
        statement->setInt(16, 0);
        statement->setInt(17, 0);
        statement->setString(18, "");
        statement->setInt64(19, now);
        statement->setInt64(20, now);
        statement->executeUpdate();
    } else {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "INSERT INTO submissions "
                "(submission_id, user_id, username_snapshot, problem_id, problem_id_text, language, source_code, status, final_status, accepted, detail, compile_success, compile_stdout, compile_stderr, total_time_used_ms, peak_memory_used_kb, system_message, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")};
        statement->setString(1, submission_id);
        statement->setInt64(2, *user_id);
        statement->setString(3, username);
        statement->setInt64(4, std::stoll(request.problem_id));
        statement->setString(5, request.problem_id);
        statement->setString(6, request.language);
        statement->setString(7, request.source_code);
        statement->setString(8, status);
        statement->setString(9, status);
        statement->setBoolean(10, false);
        statement->setString(11, detail);
        statement->setBoolean(12, false);
        statement->setString(13, "");
        statement->setString(14, "");
        statement->setInt(15, 0);
        statement->setInt(16, 0);
        statement->setString(17, "");
        statement->setInt64(18, now);
        statement->setInt64(19, now);
        statement->executeUpdate();
    }

}

// 覆盖更新提交主状态，并重建逐点评测明细以保持数据库结果与最新判题一致。
void SubmissionRepository::update_submission(const oj::common::SubmissionResult& result) const {
    auto connection = mysql_client_.create_connection();
    const auto submission_db_id = find_db_id(*connection, result.submission_id);
    if (!submission_db_id) {
        throw std::runtime_error("submission not found in mysql: " + result.submission_id);
    }

    const auto now = unix_now();
    const std::string final_status =
        (result.status == "QUEUED" || result.status == "RUNNING")
            ? result.status
            : std::string{oj::protocol::to_string(result.judge_response.final_status)};

    std::optional<std::int64_t> redis_assignment_id;
    std::optional<std::int64_t> redis_user_id;
    std::optional<std::int64_t> redis_rank_score;

    try {
        connection->setAutoCommit(false);

        auto update_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "UPDATE submissions "
                "SET status = ?, final_status = ?, accepted = ?, detail = ?, "
                "compile_success = ?, compile_stdout = ?, compile_stderr = ?, "
                "total_time_used_ms = ?, peak_memory_used_kb = ?, system_message = ?, "
                "updated_at = ? "
                "WHERE id = ?")};
        update_statement->setString(1, result.status);
        update_statement->setString(2, final_status);
        update_statement->setBoolean(3, result.accepted);
        update_statement->setString(4, result.detail);
        update_statement->setBoolean(5, result.judge_response.compile_success);
        update_statement->setString(6, result.judge_response.compile_stdout);
        update_statement->setString(7, result.judge_response.compile_stderr);
        update_statement->setInt(8, result.judge_response.total_time_used_ms);
        update_statement->setInt(9, result.judge_response.peak_memory_used_kb);
        update_statement->setString(10, result.judge_response.system_message);
        update_statement->setInt64(11, now);
        update_statement->setInt64(12, *submission_db_id);
        update_statement->executeUpdate();

        auto delete_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement("DELETE FROM submission_testcases WHERE submission_db_id = ?")};
        delete_statement->setInt64(1, *submission_db_id);
        delete_statement->executeUpdate();

        auto insert_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "INSERT INTO submission_testcases "
                "(submission_db_id, case_no, status, time_used_ms, memory_used_kb, input_data, expected_output, actual_output, error_message) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")};

        for (std::size_t i = 0; i < result.judge_response.test_case_results.size(); ++i) {
            const auto& tc = result.judge_response.test_case_results[i];
            insert_statement->setInt64(1, *submission_db_id);
            insert_statement->setInt(2, static_cast<int>(i + 1));
            insert_statement->setString(3, std::string{oj::protocol::to_string(tc.status)});
            insert_statement->setInt(4, tc.time_used_ms);
            insert_statement->setInt(5, tc.memory_used_kb);
            insert_statement->setString(6, tc.input);
            insert_statement->setString(7, tc.expected_output);
            insert_statement->setString(8, tc.actual_output);
            insert_statement->setString(9, tc.error_message);
            insert_statement->executeUpdate();
        }

        const auto assignment_context =
            load_submission_assignment_context(*connection, *submission_db_id);
        if (assignment_context &&
            should_count_for_assignment_leaderboard(*assignment_context)) {
            const auto snapshot =
                build_problem_stats_snapshot(*connection, *assignment_context);

            upsert_problem_stats_snapshot(
                *connection,
                *assignment_context,
                snapshot,
                now);

            const auto rank_snapshot =
                build_rank_stats_snapshot(
                    *connection,
                    assignment_context->assignment_id,
                    assignment_context->user_id);

            upsert_rank_stats_snapshot(
                *connection,
                *assignment_context,
                rank_snapshot,
                now);

            redis_assignment_id = assignment_context->assignment_id;
            redis_user_id = assignment_context->user_id;
            redis_rank_score = rank_snapshot.rank_score;
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

    if (redis_assignment_id && redis_user_id && redis_rank_score) {
        const oj::common::RedisConfig redis_config{};
        RedisClient redis_client{redis_config};
        const auto key = assignment_rank_zset_key(*redis_assignment_id);
        redis_client.zadd(key, static_cast<double>(*redis_rank_score), std::to_string(*redis_user_id));
        redis_client.expire(key, redis_config.assignment_leaderboard_ttl_seconds);
    }
}

// 读取一次提交的主记录和逐点评测明细，恢复成完整的提交结果对象。
std::optional<StoredSubmission> SubmissionRepository::find_submission(const std::string& submission_id) const {
    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT id, submission_id, username_snapshot, problem_id_text, language, source_code, status, final_status, accepted, detail, compile_success, compile_stdout, compile_stderr, total_time_used_ms, peak_memory_used_kb, system_message, created_at, updated_at "
            "FROM submissions WHERE submission_id = ?")};
    statement->setString(1, submission_id);
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next()) {
        return std::nullopt;
    }

    const auto submission_db_id = result->getInt64("id");
    auto stored = build_submission_from_row(*result);
    stored.result.judge_response.submission_id = std::stoll(submission_id.substr(submission_id.find('-') + 1));
    load_testcases(*connection, submission_db_id, stored);
    return stored;
}

// 在查询单次提交时额外校验归属用户，避免用户访问到别人的结果。
std::optional<StoredSubmission> SubmissionRepository::find_submission_for_user(const std::string& submission_id,
                                                                               const std::string& username) const {
    const auto stored = find_submission(submission_id);
    if (!stored || stored->username != username) {
        return std::nullopt;
    }
    return stored;
}

// 按时间倒序列出某个用户的最近提交摘要，供提交列表页展示。
std::vector<oj::common::SubmissionListItem> SubmissionRepository::list_submissions_for_user(const std::string& username,
                                                                                            std::size_t limit) const {
    auto connection = mysql_client_.create_connection();
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT submission_id, problem_id_text, language, status, final_status, accepted, detail, total_time_used_ms, peak_memory_used_kb, created_at "
            "FROM submissions WHERE username_snapshot = ? ORDER BY created_at DESC LIMIT ?")};
    statement->setString(1, username);
    statement->setInt(2, static_cast<int>(limit));
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};

    std::vector<oj::common::SubmissionListItem> items;
    while (result->next()) {
        oj::common::SubmissionListItem item;
        item.submission_id = result->getString("submission_id");
        item.problem_id = result->getString("problem_id_text");
        item.language = result->getString("language");
        item.status = result->getString("status");
        item.final_status = result->getString("final_status");
        item.accepted = result->getBoolean("accepted");
        item.detail = result->getString("detail");
        item.total_time_used_ms = result->getInt("total_time_used_ms");
        item.peak_memory_used_kb = result->getInt("peak_memory_used_kb");
        item.created_at = result->getInt64("created_at");
        items.push_back(std::move(item));
    }
    return items;
}

namespace {

std::vector<oj::common::ProblemUserStatus> collect_problem_statuses(
    sql::PreparedStatement& statement) {
    auto result = std::unique_ptr<sql::ResultSet>{statement.executeQuery()};

    std::map<std::string, oj::common::ProblemUserStatus> by_problem;

    while (result->next()) {
        const std::string problem_id =
            std::to_string(result->getInt64("problem_id"));

        const std::string final_status = result->getString("final_status");
        const bool accepted = result->getBoolean("accepted");
        const auto created_at = result->getInt64("created_at");

        auto iter = by_problem.find(problem_id);

        if (iter == by_problem.end()) {
            oj::common::ProblemUserStatus status;
            status.problem_id = problem_id;
            status.has_submission = true;
            status.status = final_status;
            status.accepted = false;
            status.last_submitted_at = created_at;

            iter = by_problem.emplace(problem_id, std::move(status)).first;
        }

        if (accepted) {
            iter->second.accepted = true;
            iter->second.status = "ACCEPTED";
        }
    }

    std::vector<oj::common::ProblemUserStatus> items;
    items.reserve(by_problem.size());

    for (auto& entry : by_problem) {
        items.push_back(std::move(entry.second));
    }

    return items;
}

} // namespace

std::vector<oj::common::ProblemUserStatus>
SubmissionRepository::list_problem_statuses_for_user(const std::string& username) const {
    auto connection = mysql_client_.create_connection();

    const auto user_id = find_user_id(*connection, username);
    if (!user_id) {
        return {};
    }

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT problem_id, final_status, accepted, created_at "
            "FROM submissions "
            "WHERE user_id = ? "
            "ORDER BY problem_id ASC, created_at DESC, id DESC")
    };

    statement->setInt64(1, *user_id);
    return collect_problem_statuses(*statement);
}

std::vector<oj::common::ProblemUserStatus>
SubmissionRepository::list_problem_statuses_for_user_in_assignment(
    const std::string& username,
    std::int64_t assignment_id) const {
    if (assignment_id <= 0) {
        throw std::runtime_error("assignment_id must be positive");
    }

    auto connection = mysql_client_.create_connection();

    const auto user_id = find_user_id(*connection, username);
    if (!user_id) {
        return {};
    }

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT problem_id, final_status, accepted, created_at "
            "FROM submissions "
            "WHERE user_id = ? AND assignment_id = ? "
            "ORDER BY problem_id ASC, created_at DESC, id DESC")
    };

    statement->setInt64(1, *user_id);
    statement->setInt64(2, assignment_id);
    return collect_problem_statuses(*statement);
}


} // namespace oj::server
