#pragma once

#include "common/platform_config.h"
#include "common/platform_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace oj::dispatcher {

std::string build_worker_failure_detail(const std::string& reason);
void mark_submission_system_error(oj::common::SubmissionResult& record,
                                  const std::string& system_message);

std::vector<oj::common::JudgeWorkerEndpoint> parse_worker_endpoints(const std::string& text);

class RoundRobinSelector {
public:
    explicit RoundRobinSelector(std::vector<oj::common::JudgeWorkerEndpoint> endpoints = {});

    const oj::common::JudgeWorkerEndpoint& next();
    std::size_t size() const noexcept;

private:
    std::vector<oj::common::JudgeWorkerEndpoint> endpoints_;
    std::size_t next_index_{0};
};

} // namespace oj::dispatcher