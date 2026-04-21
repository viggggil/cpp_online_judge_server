#pragma once

#include "common/protocol.hpp"

#include <string>
#include <vector>

namespace oj::common {

struct ProblemSummary {
    std::string id;
    std::string title;
    std::string difficulty;
};

struct SubmissionRequest {
    std::string problem_id;
    std::string language;
    std::string source_code;
};

struct SubmissionResult {
    std::string submission_id;
    std::string status;
    std::string detail;
    bool accepted{false};
    std::string username;
    std::string problem_id;
    std::string language;
    std::string source_code;
    oj::protocol::JudgeResponse judge_response;
};

struct SubmissionListItem {
    std::string submission_id;
    std::string problem_id;
    std::string language;
    std::string status;
    std::string final_status;
    std::string detail;
    bool accepted{false};
    std::int64_t created_at{0};
    std::int32_t total_time_used_ms{0};
    std::int32_t peak_memory_used_kb{0};
};

struct SubmissionQueueTask {
    std::string submission_id;
    std::string problem_id;
    std::string language;
    std::string source_code;
};

} // namespace oj::common
