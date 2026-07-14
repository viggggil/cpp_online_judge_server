#pragma once

#include "common/platform_config.h"

#include <cstdint>
#include <string>
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
    double confidence{0};
    std::string model;
    std::string provider;
    std::int64_t generated_at{0};
};

class AgentClient {
public:
    AgentClient();
    explicit AgentClient(oj::common::AgentServiceConfig config);

    AgentDiagnosisResponse create_diagnosis(
        const std::string& request_id,
        const crow::json::wvalue& payload) const;

private:
    oj::common::AgentServiceConfig config_;
};

} // namespace oj::server
