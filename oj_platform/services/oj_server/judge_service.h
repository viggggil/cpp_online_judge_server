#pragma once

#include "common/platform_types.h"

#include <filesystem>

namespace oj::server {

class JudgeService {
public:
    JudgeService(std::filesystem::path problems_root = "problems",
                 std::filesystem::path submissions_root = "runtime/submissions");

    oj::common::SubmissionResult submit(const oj::common::SubmissionRequest& request) const;

private:
    std::filesystem::path problems_root_;
    std::filesystem::path submissions_root_;
};

} // namespace oj::server
