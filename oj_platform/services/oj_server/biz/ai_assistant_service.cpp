#include "services/oj_server/biz/ai_assistant_service.h"

#include "services/oj_server/data/conversation_repository.h"
#include "services/oj_server/data/problem_repository.h"
#include "services/oj_server/data/submission_repository.h"

#include <chrono>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace oj::server {

namespace {

std::string make_id(const std::string& prefix) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << prefix << "_" << tick << "_" << std::hex << rng();
    return out.str();
}

std::string truncate_title(const std::string& value) {
    if (value.empty()) {
        return "AI 诊断";
    }
    if (value.size() <= 120) {
        return value;
    }
    return value.substr(0, 120);
}

std::string join_text(const std::vector<std::string>& items, const std::string& separator) {
    std::ostringstream out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << items[i];
    }
    return out.str();
}

std::string build_assistant_content(const AgentDiagnosisResponse& diagnosis) {
    std::ostringstream out;
    out << diagnosis.summary << "\n\n" << diagnosis.analysis;
    if (!diagnosis.hints.empty()) {
        out << "\n\n提示：";
        for (const auto& hint : diagnosis.hints) {
            out << "\n- " << hint;
        }
    }
    return out.str();
}

crow::json::wvalue make_problem_json(const oj::protocol::ProblemDetail& problem) {
    crow::json::wvalue body;
    body["problem_id"] = problem.id;
    body["title"] = problem.title;
    body["description_markdown"] = problem.statement_markdown;
    body["input_description"] = "";
    body["output_description"] = "";
    body["public_examples"] = crow::json::wvalue::list{};

    crow::json::wvalue::list tags;
    for (const auto& tag : problem.tags) {
        tags.push_back(crow::json::wvalue(tag));
    }
    body["tags"] = std::move(tags);
    body["difficulty"] = "";
    body["time_limit_ms"] = problem.time_limit_ms;
    body["memory_limit_mb"] = problem.memory_limit_mb;
    return body;
}

crow::json::wvalue make_submission_json(const AiSubmissionContext& submission) {
    crow::json::wvalue body;
    body["submission_id"] = submission.submission_id;
    body["problem_id"] = submission.problem_id;
    body["owner_user_id"] = submission.owner_user_id;
    body["language"] = submission.language;
    body["source_code"] = submission.source_code;
    body["judge_status"] = submission.judge_status;
    body["compiler_output"] = submission.compiler_output;
    body["runtime_stderr"] = submission.runtime_stderr;
    body["execution_time_ms"] = submission.execution_time_ms;
    body["memory_usage_kb"] = submission.memory_usage_kb;
    body["submitted_at"] = submission.submitted_at;
    return body;
}

} // namespace

AssistantDiagnosisResult AiAssistantService::diagnose(
    const AuthenticatedUser& user,
    const AssistantDiagnosisRequest& request) const {
    if (request.problem_id <= 0) {
        throw std::runtime_error("problem_id must be positive");
    }
    if (request.submission_id.empty()) {
        throw std::runtime_error("submission_id is required");
    }
    if (request.hint_level < 1 || request.hint_level > 3) {
        throw std::runtime_error("hint_level must be 1, 2 or 3");
    }

    AuthService auth_service;
    const auto user_id = auth_service.find_user_id(user.username);
    if (!user_id) {
        throw std::runtime_error("user not found");
    }

    ProblemRepository problem_repository;
    const auto problem = problem_repository.find_detail(request.problem_id);
    if (!problem) {
        throw std::runtime_error("problem not found");
    }

    SubmissionRepository submission_repository;
    const auto submission =
        submission_repository.find_ai_submission_for_user(*user_id, request.submission_id);
    if (!submission) {
        throw std::runtime_error("submission not found or not accessible");
    }
    if (submission->problem_id != request.problem_id) {
        throw std::runtime_error("submission does not belong to problem");
    }

    const auto request_id = make_id("req");
    crow::json::wvalue payload;
    payload["user"]["user_id"] = *user_id;
    payload["problem"] = make_problem_json(*problem);
    payload["submission"] = make_submission_json(*submission);
    payload["conversation"]["conversation_id"] = nullptr;
    payload["conversation"]["history"] = crow::json::wvalue::list{};
    payload["hint_level"] = request.hint_level;
    payload["question"] = request.question;

    AgentClient agent_client;
    const auto started_at = std::chrono::steady_clock::now();
    auto diagnosis = agent_client.create_diagnosis(request_id, payload);
    const auto latency_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at)
            .count());

    const auto conversation_id = make_id("conv");
    const auto message_id = make_id("msg");

    CreateConversationWithMessageRequest create_request;
    create_request.conversation_id = conversation_id;
    create_request.message_id = message_id;
    create_request.user_id = *user_id;
    create_request.problem_id = request.problem_id;
    create_request.submission_db_id = submission->submission_db_id;
    create_request.submission_id = submission->submission_id;
    create_request.title = truncate_title(
        request.question.empty() ? diagnosis.summary : request.question);
    create_request.hint_level = request.hint_level;
    create_request.request_id = diagnosis.request_id.empty() ? request_id : diagnosis.request_id;
    create_request.user_content = request.question.empty() ? "请诊断这次提交。" : request.question;
    create_request.assistant_content = build_assistant_content(diagnosis);
    create_request.model = diagnosis.model;
    create_request.provider = diagnosis.provider;
    create_request.finish_reason = "";
    create_request.latency_ms = latency_ms;
    create_request.knowledge_points_text = join_text(diagnosis.knowledge_points, ",");
    create_request.sources_json = "[]";
    create_request.safety_flags_json = "{}";
    create_request.error_type = diagnosis.error_type;
    create_request.confidence = diagnosis.confidence;

    ConversationRepository conversation_repository;
    const auto stored =
        conversation_repository.create_conversation_with_first_message(create_request);

    return AssistantDiagnosisResult{
        stored.conversation_id,
        stored.message_id,
        stored.round_no,
        std::move(diagnosis),
    };
}

} // namespace oj::server
