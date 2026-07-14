#pragma once

#include "services/oj_server/data/mysql_client.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oj::server {

struct AiConversationSummary {
    std::string conversation_id;
    std::int64_t user_id{0};
    std::int64_t problem_id{0};
    std::optional<std::string> submission_id;
    std::string title;
    int hint_level{2};
    int round_count{0};
    std::string status;
    std::int64_t last_message_at{0};
    std::int64_t created_at{0};
    std::int64_t updated_at{0};
};

struct AiMessageRecord {
    std::string message_id;
    int round_no{0};
    int hint_level{2};
    std::string request_id;
    std::string user_content;
    std::string assistant_content;
    std::string model;
    std::string provider;
    std::string finish_reason;
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    int latency_ms{0};
    std::string knowledge_points_text;
    std::string sources_json;
    std::string safety_flags_json;
    std::string error_type;
    std::optional<double> confidence;
    std::int64_t created_at{0};
};

struct AiConversationDetail {
    AiConversationSummary conversation;
    std::vector<AiMessageRecord> messages;
};

struct CreateConversationWithMessageRequest {
    std::string conversation_id;
    std::string message_id;
    std::int64_t user_id{0};
    std::int64_t problem_id{0};
    std::optional<std::int64_t> submission_db_id;
    std::optional<std::string> submission_id;
    std::string title;
    int hint_level{2};
    std::string request_id;
    std::string user_content;
    std::string assistant_content;
    std::string model;
    std::string provider;
    std::string finish_reason;
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    int latency_ms{0};
    std::string knowledge_points_text;
    std::string sources_json{"[]"};
    std::string safety_flags_json{"{}"};
    std::string error_type;
    std::optional<double> confidence;
};

struct StoredConversationMessage {
    std::string conversation_id;
    std::string message_id;
    int round_no{1};
};

class ConversationRepository {
public:
    ConversationRepository();
    explicit ConversationRepository(MySqlClient mysql_client);

    std::vector<AiConversationSummary> list_for_user(
        std::int64_t user_id,
        std::optional<std::int64_t> problem_id,
        int limit) const;

    std::optional<AiConversationDetail> find_for_user(
        std::int64_t user_id,
        const std::string& conversation_id) const;
    StoredConversationMessage create_conversation_with_first_message(
        const CreateConversationWithMessageRequest& request) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
