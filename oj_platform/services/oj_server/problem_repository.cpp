#include "services/oj_server/problem_repository.h"

namespace oj::server {

std::vector<oj::common::ProblemSummary> ProblemRepository::list() const {
    return {
        {"1000", "A + B Problem", "easy"},
        {"1001", "Maximum Subarray", "medium"},
        {"1002", "Shortest Path", "hard"},
    };
}

} // namespace oj::server

