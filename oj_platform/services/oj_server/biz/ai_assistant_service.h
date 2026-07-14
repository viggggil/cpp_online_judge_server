#pragma once

#include "services/oj_server/biz/agent_client.h"
#include "services/oj_server/biz/auth_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace oj::server {

struct AssistantDiagnosisRequest {
    std::int64_t problem_id{0};
    std::string submission_id;
    int hint_level{2};
    std::string question;
};

struct AssistantDiagnosisResult {
    std::string conversation_id;
    std::string message_id;
    int round_no{1};
    AgentDiagnosisResponse diagnosis;
};

class AiAssistantService {
public:
    AssistantDiagnosisResult diagnose(
        const AuthenticatedUser& user,
        const AssistantDiagnosisRequest& request) const;
};

} // namespace oj::server
