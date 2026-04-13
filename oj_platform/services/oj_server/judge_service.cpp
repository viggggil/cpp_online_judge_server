#include "services/oj_server/judge_service.h"

#include "services/judge_worker/judge_core.h"
#include "services/oj_server/problem_repository.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace oj::server {

namespace {

std::int64_t parse_problem_id(const std::string& problem_id_text) {
    return std::stoll(problem_id_text);
}

oj::protocol::LanguageType parse_language(const std::string& language_text) {
    if (language_text == "cpp" || language_text == "c++" || language_text == "cpp17") {
        return oj::protocol::LanguageType::cpp;
    }
    throw std::runtime_error("unsupported language: " + language_text);
}

std::string judge_status_to_submission_status(oj::protocol::JudgeStatus status) {
    return std::string{oj::protocol::to_string(status)};
}

std::string build_submission_detail(const oj::protocol::JudgeResponse& response) {
    std::ostringstream output;
    output << "submission judged with status " << oj::protocol::to_string(response.final_status);
    if (!response.system_message.empty()) {
        output << "; system_message=" << response.system_message;
    }
    if (!response.compile_stderr.empty()) {
        output << "; compile_stderr=" << response.compile_stderr;
    }
    return output.str();
}

void persist_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
    output << content;
}

std::string build_submission_record_json(const oj::common::SubmissionRequest& request,
                                         const oj::common::SubmissionResult& result) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"submission_id\": \"" << result.submission_id << "\",\n";
    out << "  \"problem_id\": \"" << result.problem_id << "\",\n";
    out << "  \"language\": \"" << result.language << "\",\n";
    out << "  \"status\": \"" << result.status << "\",\n";
    out << "  \"accepted\": " << (result.accepted ? "true" : "false") << ",\n";
    out << "  \"detail\": \"" << result.detail << "\",\n";
    out << "  \"compile_success\": " << (result.judge_response.compile_success ? "true" : "false") << ",\n";
    out << "  \"final_status\": \"" << oj::protocol::to_string(result.judge_response.final_status) << "\"\n";
    out << "}\n";
    (void)request;
    return out.str();
}

} // namespace

JudgeService::JudgeService(std::filesystem::path problems_root,
                           std::filesystem::path submissions_root)
    : problems_root_(std::move(problems_root)),
      submissions_root_(std::move(submissions_root)) {}

oj::common::SubmissionResult JudgeService::submit(const oj::common::SubmissionRequest& request) const {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto submission_id = "sub-" + std::to_string(tick);

    oj::common::SubmissionResult result;
    result.submission_id = submission_id;
    result.problem_id = request.problem_id;
    result.language = request.language;

    try {
        ProblemRepository repository{problems_root_};
        const auto problem_id = parse_problem_id(request.problem_id);
        const auto detail = repository.find_detail(problem_id);
        if (!detail) {
            result.status = "NOT_FOUND";
            result.detail = "problem " + request.problem_id + " does not exist";
            persist_text_file(submissions_root_ / submission_id / "result.json",
                              build_submission_record_json(request, result));
            return result;
        }

        oj::protocol::JudgeRequest judge_request;
        judge_request.submission_id = tick;
        judge_request.problem_id = problem_id;
        judge_request.language = parse_language(request.language);
        judge_request.source_code = request.source_code;
        judge_request.time_limit_ms = detail->time_limit_ms;
        judge_request.memory_limit_mb = detail->memory_limit_mb;
        judge_request.test_cases = repository.load_test_cases(problem_id);

        oj::worker::JudgeCore judge_core;
        result.judge_response = judge_core.judge(judge_request);
        result.status = judge_status_to_submission_status(result.judge_response.final_status);
        result.accepted = (result.judge_response.final_status == oj::protocol::JudgeStatus::ok);
        result.detail = build_submission_detail(result.judge_response);
    } catch (const std::exception& ex) {
        result.status = "SYSTEM_ERROR";
        result.detail = ex.what();
        result.judge_response.final_status = oj::protocol::JudgeStatus::system_error;
        result.judge_response.system_message = ex.what();
    } catch (...) {
        result.status = "SYSTEM_ERROR";
        result.detail = "unknown judge service error";
        result.judge_response.final_status = oj::protocol::JudgeStatus::system_error;
        result.judge_response.system_message = "unknown judge service error";
    }

    persist_text_file(submissions_root_ / submission_id / "source.cpp", request.source_code);
    persist_text_file(submissions_root_ / submission_id / "result.json",
                      build_submission_record_json(request, result));
    return result;
}

} // namespace oj::server

