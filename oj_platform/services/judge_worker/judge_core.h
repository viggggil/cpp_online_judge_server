#pragma once

#include "common/protocol.hpp"

#include <filesystem>
#include <string>

namespace oj::worker {

class JudgeCore {
public:
    oj::protocol::JudgeResponse judge(const oj::protocol::JudgeRequest& request) const;

private:
    std::filesystem::path prepare_work_directory(std::int64_t submission_id) const;
    oj::protocol::TestCaseResult run_single_testcase(const std::filesystem::path& executable_path,
                                                     const std::filesystem::path& submission_work_directory,
                                                     std::size_t case_index,
                                                     const oj::protocol::TestCase& test_case,
                                                     std::int32_t time_limit_ms,
                                                     std::int32_t memory_limit_mb) const;
    void summarize_results(oj::protocol::JudgeResponse& response) const;
    static std::string normalize_output(std::string text);
};

} // namespace oj::worker
