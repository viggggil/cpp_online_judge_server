#include "common/protocol_json.h"

#include <crow/json.h>

#include <stdexcept>
#include <string>

namespace oj::common {

namespace {

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

oj::protocol::LanguageType parse_language(const std::string& text) {
    if (text == "cpp" || text == "c++" || text == "cpp17") {
        return oj::protocol::LanguageType::cpp;
    }
    throw std::runtime_error("unsupported language: " + text);
}

} // namespace

// 将判题请求编码成调度服务和执行服务之间传输的 JSON 数据。
std::string serialize_judge_request(const oj::protocol::JudgeRequest& request) {
    crow::json::wvalue body;
    body["submission_id"] = request.submission_id;
    body["problem_id"] = request.problem_id;
    body["language"] = std::string{oj::protocol::to_string(request.language)};
    body["source_code"] = request.source_code;
    body["time_limit_ms"] = request.time_limit_ms;
    body["memory_limit_mb"] = request.memory_limit_mb;

    crow::json::wvalue::list items;
    for (const auto& test_case : request.test_cases) {
        crow::json::wvalue item;
        item["input"] = test_case.input;
        item["expected_output"] = test_case.expected_output;
        item["input_object_key"] = test_case.input_object_key;
        item["output_object_key"] = test_case.output_object_key;
        item["input_sha256"] = test_case.input_sha256;
        item["output_sha256"] = test_case.output_sha256;
        item["input_size_bytes"] = test_case.input_size_bytes;
        item["output_size_bytes"] = test_case.output_size_bytes;
        items.push_back(std::move(item));
    }
    body["test_cases"] = std::move(items);
    return body.dump();
}

// 从跨服务传输的 JSON 中还原判题请求，并补齐缺省字段。
oj::protocol::JudgeRequest deserialize_judge_request(const std::string& payload) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("invalid json payload");
    }

    oj::protocol::JudgeRequest request;
    request.submission_id = json.has("submission_id") ? json["submission_id"].i() : 0;
    request.problem_id = json.has("problem_id") ? json["problem_id"].i() : 0;
    request.language = json.has("language") ? parse_language(std::string{json["language"].s()})
                                             : oj::protocol::LanguageType::cpp;
    request.source_code = json.has("source_code") ? std::string{json["source_code"].s()} : std::string{};
    request.time_limit_ms = json.has("time_limit_ms") ? json["time_limit_ms"].i() : request.time_limit_ms;
    request.memory_limit_mb = json.has("memory_limit_mb") ? json["memory_limit_mb"].i() : request.memory_limit_mb;

    if (json.has("test_cases") && json["test_cases"].t() == crow::json::type::List) {
        for (const auto& item : json["test_cases"]) {
            oj::protocol::TestCase test_case;
            if (item.has("input")) {
                test_case.input = item["input"].s();
            }
            if (item.has("expected_output")) {
                test_case.expected_output = item["expected_output"].s();
            }
            if (item.has("input_object_key")) {
                test_case.input_object_key = item["input_object_key"].s();
            }
            if (item.has("output_object_key")) {
                test_case.output_object_key = item["output_object_key"].s();
            }
            if (item.has("input_sha256")) {
                test_case.input_sha256 = item["input_sha256"].s();
            }
            if (item.has("output_sha256")) {
                test_case.output_sha256 = item["output_sha256"].s();
            }
            if (item.has("input_size_bytes")) {
                test_case.input_size_bytes = item["input_size_bytes"].i();
            }
            if (item.has("output_size_bytes")) {
                test_case.output_size_bytes = item["output_size_bytes"].i();
            }
            request.test_cases.push_back(std::move(test_case));
        }
    }
    return request;
}

// 将判题结果编码成便于回传和持久化的 JSON 结构。
std::string serialize_judge_response(const oj::protocol::JudgeResponse& response) {
    crow::json::wvalue body;
    body["submission_id"] = response.submission_id;
    body["final_status"] = std::string{oj::protocol::to_string(response.final_status)};
    body["compile_success"] = response.compile_success;
    body["compile_stdout"] = response.compile_stdout;
    body["compile_stderr"] = response.compile_stderr;
    body["total_time_used_ms"] = response.total_time_used_ms;
    body["peak_memory_used_kb"] = response.peak_memory_used_kb;
    body["system_message"] = response.system_message;

    crow::json::wvalue::list items;
    for (const auto& tc : response.test_case_results) {
        crow::json::wvalue item;
        item["status"] = std::string{oj::protocol::to_string(tc.status)};
        item["input"] = tc.input;
        item["time_used_ms"] = tc.time_used_ms;
        item["memory_used_kb"] = tc.memory_used_kb;
        item["actual_output"] = tc.actual_output;
        item["expected_output"] = tc.expected_output;
        item["error_message"] = tc.error_message;
        items.push_back(std::move(item));
    }
    body["test_case_results"] = std::move(items);
    return body.dump();
}

// 解析 judge_worker 返回的 JSON，并恢复编译信息与测试点结果明细。
oj::protocol::JudgeResponse deserialize_judge_response(const std::string& payload) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("judge_worker returned invalid json");
    }

    oj::protocol::JudgeResponse response;
    response.submission_id = json.has("submission_id") ? json["submission_id"].i() : 0;
    response.final_status = json.has("final_status")
                                ? parse_judge_status(std::string{json["final_status"].s()})
                                : oj::protocol::JudgeStatus::system_error;
    response.compile_success = json.has("compile_success") && json["compile_success"].b();
    response.compile_stdout = json.has("compile_stdout") ? std::string{json["compile_stdout"].s()} : std::string{};
    response.compile_stderr = json.has("compile_stderr") ? std::string{json["compile_stderr"].s()} : std::string{};
    response.total_time_used_ms = json.has("total_time_used_ms") ? json["total_time_used_ms"].i() : 0;
    response.peak_memory_used_kb = json.has("peak_memory_used_kb") ? json["peak_memory_used_kb"].i() : 0;
    response.system_message = json.has("system_message") ? std::string{json["system_message"].s()} : std::string{};

    if (json.has("test_case_results") && json["test_case_results"].t() == crow::json::type::List) {
        for (const auto& item : json["test_case_results"]) {
            oj::protocol::TestCaseResult result;
            result.status = item.has("status") ? parse_judge_status(std::string{item["status"].s()}) : oj::protocol::JudgeStatus::system_error;
            result.input = item.has("input") ? std::string{item["input"].s()} : std::string{};
            result.time_used_ms = item.has("time_used_ms") ? item["time_used_ms"].i() : 0;
            result.memory_used_kb = item.has("memory_used_kb") ? item["memory_used_kb"].i() : 0;
            result.actual_output = item.has("actual_output") ? std::string{item["actual_output"].s()} : std::string{};
            result.expected_output = item.has("expected_output") ? std::string{item["expected_output"].s()} : std::string{};
            result.error_message = item.has("error_message") ? std::string{item["error_message"].s()} : std::string{};
            response.test_case_results.push_back(std::move(result));
        }
    }

    return response;
}

} // namespace oj::common
