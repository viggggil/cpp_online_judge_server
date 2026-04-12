#pragma once

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
};

} // namespace oj::common
