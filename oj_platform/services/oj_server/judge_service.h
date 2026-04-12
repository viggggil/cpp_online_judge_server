#pragma once

#include "common/platform_types.h"

namespace oj::server {

class JudgeService {
public:
    oj::common::SubmissionResult submit(const oj::common::SubmissionRequest& request) const;
};

} // namespace oj::server
