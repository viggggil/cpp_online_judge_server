#include "services/oj_server/data/conversation_repository.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

#include <chrono>
#include <memory>
#include <utility>

namespace oj::server {

namespace {

AiConversationSummary build_conversation_summary(sql::ResultSet& row) {
    AiConversationSummary item;
    item.conversation_id = row.getString("conversation_id");
    item.user_id = row.getInt64("user_id");
    item.problem_id = row.getInt64("problem_id");
    if (!row.isNull("submission_db_id")) {
        item.submission_db_id = row.getInt64("submission_db_id");
    }
    if (!row.isNull("submission_id")) {
        item.submission_id = row.getString("submission_id");
    }
    item.title = row.getString("title");
    item.hint_level = row.getInt("hint_level");
    item.round_count = row.getInt("round_count");
    item.status = row.getString("status");
    item.last_message_at = row.getInt64("last_message_at");
    item.created_at = row.getInt64("created_at");
    item.updated_at = row.getInt64("updated_at");
    return item;
}

AiMessageRecord build_message_record(sql::ResultSet& row) {
    AiMessageRecord message;
    message.message_id = row.getString("message_id");
    message.round_no = row.getInt("round_no");
    message.hint_level = row.getInt("hint_level");
    message.request_id = row.getString("request_id");
    message.user_content = row.getString("user_content");
    message.assistant_content = row.getString("assistant_content");
    message.model = row.getString("model");
    message.provider = row.getString("provider");
    message.finish_reason = row.getString("finish_reason");
    message.prompt_tokens = row.getInt("prompt_tokens");
    message.completion_tokens = row.getInt("completion_tokens");
    message.total_tokens = row.getInt("total_tokens");
    message.latency_ms = row.getInt("latency_ms");
    message.knowledge_points_text = row.getString("knowledge_points_text");
    message.sources_json =
        row.isNull("sources_json") ? "" : row.getString("sources_json");
    message.safety_flags_json =
        row.isNull("safety_flags_json") ? "" : row.getString("safety_flags_json");
    message.error_type = row.getString("error_type");
    if (!row.isNull("confidence")) {
        message.confidence = static_cast<double>(row.getDouble("confidence"));
    }
    message.created_at = row.getInt64("created_at");
    return message;
}

} // namespace

ConversationRepository::ConversationRepository()
    : mysql_client_{} {}

ConversationRepository::ConversationRepository(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t last_insert_id(sql::Connection& connection) {
    auto statement = std::unique_ptr<sql::Statement>{connection.createStatement()};
    auto result = std::unique_ptr<sql::ResultSet>{
        statement->executeQuery("SELECT LAST_INSERT_ID() AS id")};
    if (!result->next()) {
        throw std::runtime_error("failed to read last insert id");
    }
    return result->getInt64("id");
}

} // namespace

std::vector<AiConversationSummary> ConversationRepository::list_for_user(
    std::int64_t user_id,
    std::optional<std::int64_t> problem_id,
    int limit) const {
    auto connection = mysql_client_.create_connection();

    std::unique_ptr<sql::PreparedStatement> statement;
    if (problem_id) {
        statement.reset(connection->prepareStatement(
            "SELECT conversation_id, user_id, problem_id, submission_id, title, "
            "submission_db_id, hint_level, round_count, status, last_message_at, "
            "created_at, updated_at "
            "FROM ai_conversation "
            "WHERE user_id = ? AND problem_id = ? "
            "ORDER BY updated_at DESC, id DESC "
            "LIMIT ?"));
        statement->setInt64(1, user_id);
        statement->setInt64(2, *problem_id);
        statement->setInt(3, limit);
    } else {
        statement.reset(connection->prepareStatement(
            "SELECT conversation_id, user_id, problem_id, submission_id, title, "
            "submission_db_id, hint_level, round_count, status, last_message_at, "
            "created_at, updated_at "
            "FROM ai_conversation "
            "WHERE user_id = ? "
            "ORDER BY updated_at DESC, id DESC "
            "LIMIT ?"));
        statement->setInt64(1, user_id);
        statement->setInt(2, limit);
    }

    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    std::vector<AiConversationSummary> conversations;
    while (result->next()) {
        conversations.push_back(build_conversation_summary(*result));
    }
    return conversations;
}

std::optional<AiConversationDetail> ConversationRepository::find_for_user(
    std::int64_t user_id,
    const std::string& conversation_id) const {
    auto connection = mysql_client_.create_connection();
    auto conversation_statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT id, conversation_id, user_id, problem_id, submission_id, title, "
            "submission_db_id, hint_level, round_count, status, last_message_at, "
            "created_at, updated_at "
            "FROM ai_conversation "
            "WHERE conversation_id = ? AND user_id = ?")
    };
    conversation_statement->setString(1, conversation_id);
    conversation_statement->setInt64(2, user_id);

    auto conversation_result =
        std::unique_ptr<sql::ResultSet>{conversation_statement->executeQuery()};
    if (!conversation_result->next()) {
        return std::nullopt;
    }

    const auto conversation_db_id = conversation_result->getInt64("id");

    AiConversationDetail detail;
    detail.conversation = build_conversation_summary(*conversation_result);

    auto message_statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "SELECT "
            "message_id, round_no, hint_level, request_id, user_content, "
            "assistant_content, model, provider, finish_reason, prompt_tokens, "
            "completion_tokens, total_tokens, latency_ms, knowledge_points_text, "
            "sources_json, safety_flags_json, error_type, confidence, created_at "
            "FROM ai_message "
            "WHERE conversation_db_id = ? "
            "ORDER BY round_no ASC, id ASC")
    };
    message_statement->setInt64(1, conversation_db_id);

    auto message_result = std::unique_ptr<sql::ResultSet>{message_statement->executeQuery()};
    while (message_result->next()) {
        detail.messages.push_back(build_message_record(*message_result));
    }

    return detail;
}

StoredConversationMessage ConversationRepository::create_conversation_with_first_message(
    const CreateConversationWithMessageRequest& request) const {
    auto connection = mysql_client_.create_connection();
    const auto now = unix_now();

    try {
        connection->setAutoCommit(false);

        auto conversation_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "INSERT INTO ai_conversation "
                "(conversation_id, user_id, problem_id, submission_db_id, submission_id, "
                "title, hint_level, round_count, status, last_message_at, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
        };
        conversation_statement->setString(1, request.conversation_id);
        conversation_statement->setInt64(2, request.user_id);
        conversation_statement->setInt64(3, request.problem_id);
        if (request.submission_db_id) {
            conversation_statement->setInt64(4, *request.submission_db_id);
        } else {
            conversation_statement->setNull(4, sql::DataType::BIGINT);
        }
        if (request.submission_id) {
            conversation_statement->setString(5, *request.submission_id);
        } else {
            conversation_statement->setNull(5, sql::DataType::VARCHAR);
        }
        conversation_statement->setString(6, request.title);
        conversation_statement->setInt(7, request.hint_level);
        conversation_statement->setInt(8, 1);
        conversation_statement->setString(9, "active");
        conversation_statement->setInt64(10, now);
        conversation_statement->setInt64(11, now);
        conversation_statement->setInt64(12, now);
        conversation_statement->executeUpdate();

        const auto conversation_db_id = last_insert_id(*connection);

        auto message_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "INSERT INTO ai_message "
                "(message_id, conversation_db_id, round_no, hint_level, request_id, "
                "user_content, assistant_content, model, provider, finish_reason, "
                "prompt_tokens, completion_tokens, total_tokens, latency_ms, "
                "knowledge_points_text, sources_json, safety_flags_json, error_type, "
                "confidence, created_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
        };
        message_statement->setString(1, request.message_id);
        message_statement->setInt64(2, conversation_db_id);
        message_statement->setInt(3, 1);
        message_statement->setInt(4, request.hint_level);
        message_statement->setString(5, request.request_id);
        message_statement->setString(6, request.user_content);
        message_statement->setString(7, request.assistant_content);
        message_statement->setString(8, request.model);
        message_statement->setString(9, request.provider);
        message_statement->setString(10, request.finish_reason);
        message_statement->setInt(11, request.prompt_tokens);
        message_statement->setInt(12, request.completion_tokens);
        message_statement->setInt(13, request.total_tokens);
        message_statement->setInt(14, request.latency_ms);
        message_statement->setString(15, request.knowledge_points_text);
        message_statement->setString(16, request.sources_json);
        message_statement->setString(17, request.safety_flags_json);
        message_statement->setString(18, request.error_type);
        if (request.confidence) {
            message_statement->setDouble(19, *request.confidence);
        } else {
            message_statement->setNull(19, sql::DataType::DOUBLE);
        }
        message_statement->setInt64(20, now);
        message_statement->executeUpdate();

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

    return StoredConversationMessage{
        request.conversation_id,
        request.message_id,
        1,
    };
}

StoredConversationMessage ConversationRepository::append_message(
    const AppendConversationMessageRequest& request) const {
    auto connection = mysql_client_.create_connection();
    const auto now = unix_now();
    int round_no = 1;

    try {
        connection->setAutoCommit(false);

        auto conversation_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "SELECT id, round_count "
                "FROM ai_conversation "
                "WHERE conversation_id = ? AND user_id = ? "
                "FOR UPDATE")
        };
        conversation_statement->setString(1, request.conversation_id);
        conversation_statement->setInt64(2, request.user_id);

        auto conversation_result =
            std::unique_ptr<sql::ResultSet>{conversation_statement->executeQuery()};
        if (!conversation_result->next()) {
            throw std::runtime_error("conversation not found or not accessible");
        }

        const auto conversation_db_id = conversation_result->getInt64("id");
        round_no = conversation_result->getInt("round_count") + 1;
        if (round_no <= 1) {
            round_no = 1;
        }

        auto message_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "INSERT INTO ai_message "
                "(message_id, conversation_db_id, round_no, hint_level, request_id, "
                "user_content, assistant_content, model, provider, finish_reason, "
                "prompt_tokens, completion_tokens, total_tokens, latency_ms, "
                "knowledge_points_text, sources_json, safety_flags_json, error_type, "
                "confidence, created_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
        };
        message_statement->setString(1, request.message_id);
        message_statement->setInt64(2, conversation_db_id);
        message_statement->setInt(3, round_no);
        message_statement->setInt(4, request.hint_level);
        message_statement->setString(5, request.request_id);
        message_statement->setString(6, request.user_content);
        message_statement->setString(7, request.assistant_content);
        message_statement->setString(8, request.model);
        message_statement->setString(9, request.provider);
        message_statement->setString(10, request.finish_reason);
        message_statement->setInt(11, request.prompt_tokens);
        message_statement->setInt(12, request.completion_tokens);
        message_statement->setInt(13, request.total_tokens);
        message_statement->setInt(14, request.latency_ms);
        message_statement->setString(15, request.knowledge_points_text);
        message_statement->setString(16, request.sources_json);
        message_statement->setString(17, request.safety_flags_json);
        message_statement->setString(18, request.error_type);
        if (request.confidence) {
            message_statement->setDouble(19, *request.confidence);
        } else {
            message_statement->setNull(19, sql::DataType::DOUBLE);
        }
        message_statement->setInt64(20, now);
        message_statement->executeUpdate();

        auto update_statement = std::unique_ptr<sql::PreparedStatement>{
            connection->prepareStatement(
                "UPDATE ai_conversation "
                "SET hint_level = ?, round_count = ?, last_message_at = ?, updated_at = ? "
                "WHERE id = ?")
        };
        update_statement->setInt(1, request.hint_level);
        update_statement->setInt(2, round_no);
        update_statement->setInt64(3, now);
        update_statement->setInt64(4, now);
        update_statement->setInt64(5, conversation_db_id);
        update_statement->executeUpdate();

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

    return StoredConversationMessage{
        request.conversation_id,
        request.message_id,
        round_no,
    };
}

} // namespace oj::server
