#pragma once

#include "services/oj_server/biz/agent_client.h"
#include "services/oj_server/biz/auth_service.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oj::server {

struct AssistantDiagnosisRequest {
    std::int64_t problem_id{0};
    std::string submission_id;
    int hint_level{2};
    std::string question;
};

struct AssistantConversationMessageRequest {
    std::string conversation_id;
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
    using ProgressCallback = std::function<void(std::string_view stage, std::string_view message)>;
    using StreamEventCallback = AgentClient::StreamEventCallback;

    AssistantDiagnosisResult diagnose(
        const AuthenticatedUser& user,
        const AssistantDiagnosisRequest& request,
        const ProgressCallback& progress = nullptr) const;

    AssistantDiagnosisResult diagnose_stream(
        const AuthenticatedUser& user,
        const AssistantDiagnosisRequest& request,
        const ProgressCallback& progress,
        const StreamEventCallback& stream_event) const;

    AssistantDiagnosisResult continue_diagnosis_stream(
        const AuthenticatedUser& user,
        const AssistantConversationMessageRequest& request,
        const ProgressCallback& progress,
        const StreamEventCallback& stream_event) const;
};

} // namespace oj::server
