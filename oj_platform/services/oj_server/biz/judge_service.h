#pragma once

#include "common/platform_types.h"

#include <optional>

namespace oj::server {

class JudgeService {
public:
    JudgeService() = default;

    oj::common::SubmissionResult submit(const std::string& username,
                                        const oj::common::SubmissionRequest& request) const;
    std::optional<oj::common::SubmissionResult> find_submission(const std::string& username,
                                                                const std::string& submission_id) const;
    std::vector<oj::common::SubmissionListItem> list_submissions(const std::string& username) const;
};

} // namespace oj::server
