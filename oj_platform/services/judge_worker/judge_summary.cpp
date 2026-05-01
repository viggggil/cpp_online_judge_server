#include "services/judge_worker/judge_summary.h"

#include <algorithm>

namespace oj::worker {

// 汇总所有测试点结果，计算总耗时、峰值内存并确定最终判题状态。
void summarize_judge_response(oj::protocol::JudgeResponse& response) {
    response.total_time_used_ms = 0;
    response.peak_memory_used_kb = 0;

    if (response.test_case_results.empty()) {
        response.final_status = oj::protocol::JudgeStatus::system_error;
        response.system_message = "no test cases provided";
        return;
    }

    response.final_status = oj::protocol::JudgeStatus::ok;
    for (const auto& item : response.test_case_results) {
        response.total_time_used_ms += item.time_used_ms;
        response.peak_memory_used_kb = std::max(response.peak_memory_used_kb, item.memory_used_kb);

        if (item.status != oj::protocol::JudgeStatus::ok &&
            response.final_status == oj::protocol::JudgeStatus::ok) {
            response.final_status = item.status;
        }
    }
}

} // namespace oj::worker
