#pragma once

#include "common/protocol.hpp"

#include <optional>
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
    std::optional<std::int64_t> assignment_id;
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

struct ProblemUserStatus {
    std::string problem_id;

    // NONE / ACCEPTED / WRONG_ANSWER / COMPILE_ERROR / RUNTIME_ERROR / ...
    std::string status{"NONE"};

    bool has_submission{false};
    bool accepted{false};

    // Unix timestamp seconds; 0 表示没有提交
    std::int64_t last_submitted_at{0};
};

struct SubmissionQueueTask {
    std::string submission_id;
    std::string problem_id;
    std::string language;
    std::string source_code;
};

struct AssignmentLeaderboardProblemColumn {
    std::int64_t problem_id{0};
    std::string alias;
    std::string title;
    int display_order{0};
    std::int64_t accepted_user_count{0};
    std::int64_t submission_count{0};
};

struct AssignmentLeaderboardCell {
    std::int64_t problem_id{0};
    std::string alias;

    bool has_submission{false};
    bool accepted{false};

    // accepted=true 时显示 100；未通过但提交过显示 0；未提交可以不显示
    int score{0};

    std::string status{"NONE"};

    std::int64_t time_from_start_seconds{0};

    std::int64_t first_accepted_at{0};
    std::int64_t last_submitted_at{0};
    std::int64_t submission_count{0};
};

struct AssignmentLeaderboardEntry {
    int rank{0};
    std::string username;

    std::int64_t solved_count{0};
    std::int64_t score{0};
    std::int64_t penalty_seconds{0};

    std::vector<AssignmentLeaderboardCell> cells;
};

struct AssignmentLeaderboard {
    std::int64_t assignment_id{0};
    std::string title;
    std::int64_t start_at{0};
    std::int64_t end_at{0};

    std::vector<AssignmentLeaderboardProblemColumn> problems;
    std::vector<AssignmentLeaderboardEntry> entries;
};

} // namespace oj::common
