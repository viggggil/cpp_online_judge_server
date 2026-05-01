#include "services/oj_server/submission_repository.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <chrono>
#include <stdexcept>

namespace oj::server {

namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "INSERT INTO submissions (submission_id, user_id, username_snapshot, problem_id, problem_id_text, language, source_code, status, final_status, accepted, detail, compile_success, compile_stdout, compile_stderr, total_time_used_ms, peak_memory_used_kb, system_message, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")};
    const auto now = unix_now();
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

// 覆盖更新提交主状态，并重建逐点评测明细以保持数据库结果与最新判题一致。
void SubmissionRepository::update_submission(const oj::common::SubmissionResult& result) const {
    auto connection = mysql_client_.create_connection();
    const auto submission_db_id = find_db_id(*connection, result.submission_id);
    if (!submission_db_id) {
        throw std::runtime_error("submission not found in mysql: " + result.submission_id);
    }

    const std::string final_status = (result.status == "QUEUED" || result.status == "RUNNING")
                                         ? result.status
                                         : std::string{oj::protocol::to_string(result.judge_response.final_status)};

    auto update_statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "UPDATE submissions SET status = ?, final_status = ?, accepted = ?, detail = ?, compile_success = ?, compile_stdout = ?, compile_stderr = ?, total_time_used_ms = ?, peak_memory_used_kb = ?, system_message = ?, updated_at = ? WHERE id = ?")};
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
    update_statement->setInt64(11, unix_now());
    update_statement->setInt64(12, *submission_db_id);
    update_statement->executeUpdate();

    auto delete_statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement("DELETE FROM submission_testcases WHERE submission_db_id = ?")};
    delete_statement->setInt64(1, *submission_db_id);
    delete_statement->executeUpdate();

    auto insert_statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "INSERT INTO submission_testcases (submission_db_id, case_no, status, time_used_ms, memory_used_kb, input_data, expected_output, actual_output, error_message) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")};

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

} // namespace oj::server
