#pragma once

#include "common/protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace oj::worker {

struct RunResult {
    oj::protocol::JudgeStatus status{oj::protocol::JudgeStatus::system_error};
    int exit_code{-1};
    bool timed_out{false};
    bool signaled{false};
    int signal_number{0};
    std::int32_t time_used_ms{0};
    std::int32_t memory_used_kb{0};
    std::string stdout_text;
    std::string stderr_text;
    std::string error_message;
};

class RunService {
public:
    RunResult run(const std::filesystem::path& executable_path,
                  const std::string& standard_input,
                  std::int32_t time_limit_ms,
                  std::int32_t memory_limit_mb,
                  const std::filesystem::path& work_directory) const;
};

} // namespace oj::worker
