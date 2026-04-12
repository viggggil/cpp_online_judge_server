#pragma once

#include "common/platform_types.h"

namespace oj::worker {

class JudgeCore {
public:
    oj::common::SubmissionResult judge(const oj::common::SubmissionRequest& request) const;
};

} // namespace oj::worker
