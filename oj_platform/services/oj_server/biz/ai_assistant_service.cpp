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
        return "AI 助手";
    }
    if (value.size() <= 120) {
        return value;
    }
    return value.substr(0, 120);
}

std::string default_user_message(const std::string& message) {
    return message.empty() ? "你好，我想问一个编程问题。" : message;
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

std::string build_sources_json(const std::vector<AgentSourceReference>& sources) {
    crow::json::wvalue::list items;
    for (const auto& source : sources) {
        crow::json::wvalue item;
        item["document_id"] = source.document_id;
        item["source"] = source.source;
        item["title"] = source.title;
        item["knowledge_point"] = source.knowledge_point;
        item["chunk_index"] = source.chunk_index;
        item["score"] = source.score;
        items.push_back(std::move(item));
    }
    return crow::json::wvalue(std::move(items)).dump();
}

crow::json::wvalue make_chat_payload(
    std::int64_t user_id,
    const std::optional<std::int64_t>& problem_id,
    const std::optional<std::string>& submission_id,
    const std::string& conversation_id,
    crow::json::wvalue::list history,
    int hint_level,
    const std::string& message) {
    crow::json::wvalue payload;
    payload["user"]["user_id"] = user_id;
    if (conversation_id.empty()) {
        payload["conversation"]["conversation_id"] = nullptr;
    } else {
        payload["conversation"]["conversation_id"] = conversation_id;
    }
    payload["conversation"]["history"] = std::move(history);
    if (problem_id) {
        payload["initial_context"]["problem_id"] = *problem_id;
    } else {
        payload["initial_context"]["problem_id"] = nullptr;
    }
    if (submission_id && !submission_id->empty()) {
        payload["initial_context"]["submission_id"] = *submission_id;
    } else {
        payload["initial_context"]["submission_id"] = nullptr;
    }
    payload["hint_level"] = hint_level;
    payload["message"] = message;
    return payload;
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

crow::json::wvalue::list make_history_json(
    const std::vector<AiMessageRecord>& messages,
    std::size_t max_items) {
    crow::json::wvalue::list history;
    const auto start = messages.size() > max_items ? messages.size() - max_items : 0;
    for (std::size_t i = start; i < messages.size(); ++i) {
        const auto& message = messages[i];
        crow::json::wvalue item;
        item["round_no"] = message.round_no;
        item["user_content"] = message.user_content;
        item["assistant_content"] = message.assistant_content;
        history.push_back(std::move(item));
    }
    return history;
}

} // namespace

AssistantDiagnosisResult AiAssistantService::diagnose(
    const AuthenticatedUser& user,
    const AssistantDiagnosisRequest& request,
    const ProgressCallback& progress) const {
    if (progress) {
        progress("validating", "正在校验诊断请求");
    }
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

    if (progress) {
        progress("loading_problem", "正在读取题目信息");
    }
    ProblemRepository problem_repository;
    const auto problem = problem_repository.find_detail(request.problem_id);
    if (!problem) {
        throw std::runtime_error("problem not found");
    }

    if (progress) {
        progress("loading_submission", "正在读取你的提交记录");
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

    if (progress) {
        progress("retrieving_knowledge", "正在查询本地知识库");
    }
    AgentClient agent_client;
    const auto started_at = std::chrono::steady_clock::now();
    if (progress) {
        progress("generating", "正在调用模型生成中文诊断");
    }
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
    create_request.sources_json = build_sources_json(diagnosis.sources);
    create_request.safety_flags_json = "{}";
    create_request.error_type = diagnosis.error_type;
    create_request.confidence = diagnosis.confidence;

    if (progress) {
        progress("saving", "正在写入对话数据库");
    }
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

AssistantDiagnosisResult AiAssistantService::diagnose_stream(
    const AuthenticatedUser& user,
    const AssistantDiagnosisRequest& request,
    const ProgressCallback& progress,
    const StreamEventCallback& stream_event) const {
    if (progress) {
        progress("validating", "正在校验诊断请求");
    }
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

    if (progress) {
        progress("loading_problem", "正在读取题目信息");
    }
    ProblemRepository problem_repository;
    const auto problem = problem_repository.find_detail(request.problem_id);
    if (!problem) {
        throw std::runtime_error("problem not found");
    }

    if (progress) {
        progress("loading_submission", "正在读取你的提交记录");
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

    if (progress) {
        progress("retrieving_knowledge", "正在查询本地知识库");
    }
    AgentClient agent_client;
    const auto started_at = std::chrono::steady_clock::now();
    if (progress) {
        progress("generating", "正在调用模型流式生成中文诊断");
    }
    auto diagnosis = agent_client.create_diagnosis_stream(
        request_id,
        payload,
        stream_event);
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
    create_request.sources_json = build_sources_json(diagnosis.sources);
    create_request.safety_flags_json = "{}";
    create_request.error_type = diagnosis.error_type;
    create_request.confidence = diagnosis.confidence;

    if (progress) {
        progress("saving", "正在写入对话数据库");
    }
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

AssistantDiagnosisResult AiAssistantService::continue_diagnosis_stream(
    const AuthenticatedUser& user,
    const AssistantConversationMessageRequest& request,
    const ProgressCallback& progress,
    const StreamEventCallback& stream_event) const {
    if (progress) {
        progress("validating", "正在校验诊断请求");
    }
    if (request.conversation_id.empty()) {
        throw std::runtime_error("conversation_id is required");
    }
    if (request.hint_level < 1 || request.hint_level > 3) {
        throw std::runtime_error("hint_level must be 1, 2 or 3");
    }

    AuthService auth_service;
    const auto user_id = auth_service.find_user_id(user.username);
    if (!user_id) {
        throw std::runtime_error("user not found");
    }

    ConversationRepository conversation_repository;
    const auto detail =
        conversation_repository.find_for_user(*user_id, request.conversation_id);
    if (!detail) {
        throw std::runtime_error("conversation not found or not accessible");
    }
    if (!detail->conversation.submission_id || detail->conversation.submission_id->empty()) {
        throw std::runtime_error("conversation has no submission context");
    }
    if (!detail->conversation.problem_id) {
        throw std::runtime_error("conversation has no problem context");
    }

    if (progress) {
        progress("loading_problem", "正在读取题目信息");
    }
    ProblemRepository problem_repository;
    const auto problem = problem_repository.find_detail(*detail->conversation.problem_id);
    if (!problem) {
        throw std::runtime_error("problem not found");
    }

    if (progress) {
        progress("loading_submission", "正在读取你的提交记录");
    }
    SubmissionRepository submission_repository;
    const auto submission = submission_repository.find_ai_submission_for_user(
        *user_id,
        *detail->conversation.submission_id);
    if (!submission) {
        throw std::runtime_error("submission not found or not accessible");
    }
    if (submission->problem_id != *detail->conversation.problem_id) {
        throw std::runtime_error("submission does not belong to conversation problem");
    }

    const auto request_id = make_id("req");
    crow::json::wvalue payload;
    payload["user"]["user_id"] = *user_id;
    payload["problem"] = make_problem_json(*problem);
    payload["submission"] = make_submission_json(*submission);
    payload["conversation"]["conversation_id"] = request.conversation_id;
    payload["conversation"]["history"] = make_history_json(detail->messages, 8);
    payload["hint_level"] = request.hint_level;
    payload["question"] = request.question;

    if (progress) {
        progress("retrieving_knowledge", "正在查询本地知识库");
    }
    AgentClient agent_client;
    const auto started_at = std::chrono::steady_clock::now();
    if (progress) {
        progress("generating", "正在调用模型流式生成中文诊断");
    }
    auto diagnosis = agent_client.create_diagnosis_stream(
        request_id,
        payload,
        stream_event);
    const auto latency_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at)
            .count());

    const auto message_id = make_id("msg");

    AppendConversationMessageRequest append_request;
    append_request.conversation_id = request.conversation_id;
    append_request.message_id = message_id;
    append_request.user_id = *user_id;
    append_request.problem_id = detail->conversation.problem_id;
    append_request.submission_db_id = submission->submission_db_id;
    append_request.submission_id = submission->submission_id;
    append_request.title = detail->conversation.title;
    append_request.hint_level = request.hint_level;
    append_request.request_id = diagnosis.request_id.empty() ? request_id : diagnosis.request_id;
    append_request.user_content = request.question.empty() ? "请继续诊断这次提交。" : request.question;
    append_request.assistant_content = build_assistant_content(diagnosis);
    append_request.model = diagnosis.model;
    append_request.provider = diagnosis.provider;
    append_request.finish_reason = "";
    append_request.latency_ms = latency_ms;
    append_request.knowledge_points_text = join_text(diagnosis.knowledge_points, ",");
    append_request.sources_json = build_sources_json(diagnosis.sources);
    append_request.safety_flags_json = "{}";
    append_request.error_type = diagnosis.error_type;
    append_request.confidence = diagnosis.confidence;

    if (progress) {
        progress("saving", "正在写入对话数据库");
    }
    const auto stored = conversation_repository.append_message(append_request);

    return AssistantDiagnosisResult{
        stored.conversation_id,
        stored.message_id,
        stored.round_no,
        std::move(diagnosis),
    };
}

AssistantChatResult AiAssistantService::start_chat_stream(
    const AuthenticatedUser& user,
    const AssistantChatRequest& request,
    const ProgressCallback& progress,
    const StreamEventCallback& stream_event) const {
    if (progress) {
        progress("validating", "正在校验对话请求");
    }
    if (request.hint_level < 1 || request.hint_level > 3) {
        throw std::runtime_error("hint_level must be 1, 2 or 3");
    }
    if (request.message.empty()) {
        throw std::runtime_error("message is required");
    }

    AuthService auth_service;
    const auto user_id = auth_service.find_user_id(user.username);
    if (!user_id) {
        throw std::runtime_error("user not found");
    }

    std::optional<std::int64_t> problem_id = request.problem_id;
    std::optional<std::int64_t> submission_db_id;
    std::optional<std::string> submission_id = request.submission_id;

    if (problem_id && *problem_id <= 0) {
        throw std::runtime_error("problem_id must be positive");
    }

    if (submission_id && !submission_id->empty()) {
        if (progress) {
            progress("loading_submission", "正在读取你的提交记录");
        }
        SubmissionRepository submission_repository;
        const auto submission =
            submission_repository.find_ai_submission_for_user(*user_id, *submission_id);
        if (!submission) {
            throw std::runtime_error("submission not found or not accessible");
        }
        if (problem_id && submission->problem_id != *problem_id) {
            throw std::runtime_error("submission does not belong to problem");
        }
        problem_id = submission->problem_id;
        submission_db_id = submission->submission_db_id;
        submission_id = submission->submission_id;
    }

    if (problem_id) {
        if (progress) {
            progress("loading_problem", "正在读取题目信息");
        }
        ProblemRepository problem_repository;
        if (!problem_repository.find_detail(*problem_id)) {
            throw std::runtime_error("problem not found");
        }
    }

    const auto request_id = make_id("req");
    auto payload = make_chat_payload(
        *user_id,
        problem_id,
        submission_id,
        "",
        crow::json::wvalue::list{},
        request.hint_level,
        request.message);

    AgentClient agent_client;
    const auto started_at = std::chrono::steady_clock::now();
    if (progress) {
        progress("generating", "正在调用模型流式生成回答");
    }
    auto chat = agent_client.create_chat_stream(
        request_id,
        payload,
        stream_event);
    const auto latency_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at)
            .count());

    if (chat.problem_id) {
        problem_id = chat.problem_id;
    }
    if (chat.submission_id && !chat.submission_id->empty()) {
        submission_id = chat.submission_id;
    }

    const auto conversation_id = make_id("conv");
    const auto message_id = make_id("msg");

    CreateConversationWithMessageRequest create_request;
    create_request.conversation_id = conversation_id;
    create_request.message_id = message_id;
    create_request.user_id = *user_id;
    create_request.problem_id = problem_id;
    create_request.submission_db_id = submission_db_id;
    create_request.submission_id = submission_id;
    create_request.title = truncate_title(request.message);
    create_request.hint_level = request.hint_level;
    create_request.request_id = chat.request_id.empty() ? request_id : chat.request_id;
    create_request.user_content = default_user_message(request.message);
    create_request.assistant_content = chat.answer;
    create_request.model = chat.model;
    create_request.provider = chat.provider;
    create_request.finish_reason = "";
    create_request.latency_ms = latency_ms;
    create_request.knowledge_points_text = join_text(chat.knowledge_points, ",");
    create_request.sources_json = chat.raw_done_json.empty() ? build_sources_json(chat.sources) : chat.raw_done_json;
    create_request.safety_flags_json = chat.safety_flags_json;
    create_request.error_type = chat.intent;
    create_request.confidence = std::nullopt;

    if (progress) {
        progress("saving", "正在写入对话数据库");
    }
    ConversationRepository conversation_repository;
    const auto stored =
        conversation_repository.create_conversation_with_first_message(create_request);

    return AssistantChatResult{
        stored.conversation_id,
        stored.message_id,
        stored.round_no,
        std::move(chat),
    };
}

AssistantChatResult AiAssistantService::continue_chat_stream(
    const AuthenticatedUser& user,
    const AssistantChatMessageRequest& request,
    const ProgressCallback& progress,
    const StreamEventCallback& stream_event) const {
    if (progress) {
        progress("validating", "正在校验对话请求");
    }
    if (request.conversation_id.empty()) {
        throw std::runtime_error("conversation_id is required");
    }
    if (request.hint_level < 1 || request.hint_level > 3) {
        throw std::runtime_error("hint_level must be 1, 2 or 3");
    }
    if (request.message.empty()) {
        throw std::runtime_error("message is required");
    }

    AuthService auth_service;
    const auto user_id = auth_service.find_user_id(user.username);
    if (!user_id) {
        throw std::runtime_error("user not found");
    }

    ConversationRepository conversation_repository;
    const auto detail =
        conversation_repository.find_for_user(*user_id, request.conversation_id);
    if (!detail) {
        throw std::runtime_error("conversation not found or not accessible");
    }

    const auto request_id = make_id("req");
    auto payload = make_chat_payload(
        *user_id,
        detail->conversation.problem_id,
        detail->conversation.submission_id,
        request.conversation_id,
        make_history_json(detail->messages, 8),
        request.hint_level,
        request.message);

    AgentClient agent_client;
    const auto started_at = std::chrono::steady_clock::now();
    if (progress) {
        progress("generating", "正在调用模型流式生成回答");
    }
    auto chat = agent_client.create_chat_stream(
        request_id,
        payload,
        stream_event);
    const auto latency_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at)
            .count());

    const auto message_id = make_id("msg");

    AppendConversationMessageRequest append_request;
    append_request.conversation_id = request.conversation_id;
    append_request.message_id = message_id;
    append_request.user_id = *user_id;
    append_request.problem_id = chat.problem_id ? chat.problem_id : detail->conversation.problem_id;
    append_request.submission_db_id = detail->conversation.submission_db_id;
    append_request.submission_id = chat.submission_id ? chat.submission_id : detail->conversation.submission_id;
    append_request.title = detail->conversation.title;
    append_request.hint_level = request.hint_level;
    append_request.request_id = chat.request_id.empty() ? request_id : chat.request_id;
    append_request.user_content = request.message;
    append_request.assistant_content = chat.answer;
    append_request.model = chat.model;
    append_request.provider = chat.provider;
    append_request.finish_reason = "";
    append_request.latency_ms = latency_ms;
    append_request.knowledge_points_text = join_text(chat.knowledge_points, ",");
    append_request.sources_json = chat.raw_done_json.empty() ? build_sources_json(chat.sources) : chat.raw_done_json;
    append_request.safety_flags_json = chat.safety_flags_json;
    append_request.error_type = chat.intent;
    append_request.confidence = std::nullopt;

    if (progress) {
        progress("saving", "正在写入对话数据库");
    }
    const auto stored = conversation_repository.append_message(append_request);

    return AssistantChatResult{
        stored.conversation_id,
        stored.message_id,
        stored.round_no,
        std::move(chat),
    };
}

} // namespace oj::server
