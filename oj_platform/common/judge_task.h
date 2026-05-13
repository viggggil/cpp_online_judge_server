#pragma once

#include <crow/json.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace oj::common {

struct JudgeTask {
    std::string submission_id;
    std::string problem_id;
    std::string language{"cpp"};
    std::string trace_id;
    int retry_count{0};
};

inline std::string to_json_string(const JudgeTask& task) {
    crow::json::wvalue json;
    json["submission_id"] = task.submission_id;
    json["problem_id"] = task.problem_id;
    json["language"] = task.language;
    json["trace_id"] = task.trace_id;
    json["retry_count"] = task.retry_count;
    return json.dump();
}

inline JudgeTask judge_task_from_json(const std::string& payload) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("invalid judge task json");
    }

    JudgeTask task;
    task.submission_id = json.has("submission_id")
        ? std::string{json["submission_id"].s()}
        : std::string{};
    task.problem_id = json.has("problem_id")
        ? std::string{json["problem_id"].s()}
        : std::string{};
    task.language = json.has("language")
        ? std::string{json["language"].s()}
        : std::string{"cpp"};
    task.trace_id = json.has("trace_id")
        ? std::string{json["trace_id"].s()}
        : std::string{};
    task.retry_count = json.has("retry_count")
        ? static_cast<int>(json["retry_count"].i())
        : 0;

    if (task.submission_id.empty() || task.problem_id.empty()) {
        throw std::runtime_error("judge task missing submission_id or problem_id");
    }

    return task;
}

} // namespace oj::common
