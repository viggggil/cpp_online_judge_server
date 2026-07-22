#pragma once

#include "common/platform_config.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <crow/json.h>

namespace oj::server {

struct AgentSourceReference {
    std::string document_id;
    std::string source;
    std::string title;
    std::string knowledge_point;
    int chunk_index{0};
    double score{0};
};

struct AgentDiagnosisResponse {
    std::string request_id;
    std::string diagnosis_id;
    std::int64_t user_id{0};
    std::int64_t problem_id{0};
    std::string submission_id;
    std::string judge_status;
    int hint_level{2};
    std::string error_type;
    std::string summary;
    std::string analysis;
    std::vector<std::string> evidence;
    std::vector<std::string> knowledge_points;
    std::vector<std::string> hints;
    std::vector<AgentSourceReference> sources;
    double confidence{0};
    std::string model;
    std::string provider;
    std::int64_t generated_at{0};
};

struct AgentChatResponse {
    std::string request_id;
    std::int64_t user_id{0};
    std::string title;
    std::optional<std::int64_t> problem_id;
    std::optional<std::string> submission_id;
    std::string answer;
    std::string intent;
    std::vector<std::string> knowledge_points;
    std::vector<AgentSourceReference> sources;
    std::string sources_json;
    std::string safety_flags_json;
    std::string raw_done_json;
    std::string model;
    std::string provider;
    std::int64_t generated_at{0};
};

class AgentClient {
public:
    using StreamEventCallback = std::function<void(std::string_view event, std::string_view data_json)>;

    AgentClient();
    explicit AgentClient(oj::common::AgentServiceConfig config);

    AgentDiagnosisResponse create_diagnosis(
        const std::string& request_id,
        const crow::json::wvalue& payload) const;

    AgentDiagnosisResponse create_diagnosis_stream(
        const std::string& request_id,
        const crow::json::wvalue& payload,
        const StreamEventCallback& event_callback) const;

    AgentChatResponse create_chat_stream(
        const std::string& request_id,
        const crow::json::wvalue& payload,
        const StreamEventCallback& event_callback) const;

private:
    oj::common::AgentServiceConfig config_;
};

} // namespace oj::server
