#include "services/judge_worker/judge_summary.h"

#include <gtest/gtest.h>

namespace {

TEST(JudgeSummaryTest, EmptyTestcasesBecomeSystemError) {
    oj::protocol::JudgeResponse response;

    oj::worker::summarize_judge_response(response);

    EXPECT_EQ(response.final_status, oj::protocol::JudgeStatus::system_error);
    EXPECT_EQ(response.system_message, "no test cases provided");
    EXPECT_EQ(response.total_time_used_ms, 0);
    EXPECT_EQ(response.peak_memory_used_kb, 0);
}

TEST(JudgeSummaryTest, AggregatesTimeAndPeakMemoryAndKeepsOkVerdict) {
    oj::protocol::JudgeResponse response;
    response.test_case_results = {
        {oj::protocol::JudgeStatus::ok, 11, 1024, "", "", "", ""},
        {oj::protocol::JudgeStatus::ok, 13, 4096, "", "", "", ""},
        {oj::protocol::JudgeStatus::ok, 17, 2048, "", "", "", ""},
    };

    oj::worker::summarize_judge_response(response);

    EXPECT_EQ(response.final_status, oj::protocol::JudgeStatus::ok);
    EXPECT_EQ(response.total_time_used_ms, 41);
    EXPECT_EQ(response.peak_memory_used_kb, 4096);
}

TEST(JudgeSummaryTest, FirstNonOkTestcaseDeterminesFinalVerdict) {
    oj::protocol::JudgeResponse response;
    response.test_case_results = {
        {oj::protocol::JudgeStatus::ok, 3, 128, "", "", "", ""},
        {oj::protocol::JudgeStatus::wrong_answer, 5, 256, "", "", "", ""},
        {oj::protocol::JudgeStatus::time_limit_exceeded, 1000, 512, "", "", "", ""},
    };

    oj::worker::summarize_judge_response(response);

    EXPECT_EQ(response.final_status, oj::protocol::JudgeStatus::wrong_answer);
    EXPECT_EQ(response.total_time_used_ms, 1008);
    EXPECT_EQ(response.peak_memory_used_kb, 512);
}

} // namespace