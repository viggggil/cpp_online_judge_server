#include "services/oj_server/judge_service.h"

#include <chrono>
#include <string>

namespace oj::server {

oj::common::SubmissionResult JudgeService::submit(const oj::common::SubmissionRequest& request) const {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();

    return {
        "sub-" + std::to_string(tick),
        "queued",
        "submission for problem " + request.problem_id + " has been queued for worker execution"};
}

} // namespace oj::server

