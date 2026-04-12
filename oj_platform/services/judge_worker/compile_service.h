#pragma once

#include <string>

namespace oj::worker {

class CompileService {
public:
    std::string compile(const std::string& language, const std::string& source_code) const;
};

} // namespace oj::worker
