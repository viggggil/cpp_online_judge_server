#pragma once

#include <string>

namespace oj::worker {

class RunService {
public:
    std::string run(const std::string& executable_path) const;
};

} // namespace oj::worker
