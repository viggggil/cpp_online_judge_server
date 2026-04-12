#pragma once

#include "common/platform_types.h"

#include <vector>

namespace oj::server {

class ProblemRepository {
public:
    std::vector<oj::common::ProblemSummary> list() const;
};

} // namespace oj::server
