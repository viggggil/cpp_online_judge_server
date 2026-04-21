#include "services/judge_dispatcher/dispatcher_utils.h"

#include <gtest/gtest.h>

namespace {

TEST(DispatcherUtilsTest, WorkerFailureMarkedAsSystemError) {
    oj::common::SubmissionResult record;
    record.accepted = true;
    oj::dispatcher::mark_submission_system_error(record, "connect timeout while sending request");

    EXPECT_EQ(record.judge_response.final_status, oj::protocol::JudgeStatus::system_error);
    EXPECT_FALSE(record.judge_response.compile_success);
    EXPECT_FALSE(record.accepted);
    EXPECT_EQ(record.status, "SYSTEM_ERROR");
    EXPECT_EQ(record.detail, "worker timeout: connect timeout while sending request");
}

TEST(DispatcherUtilsTest, RoundRobinSelectorCyclesWorkersInOrder) {
    auto endpoints = oj::dispatcher::parse_worker_endpoints(
        "127.0.0.1:18081/api/judge,127.0.0.2:18082/api/judge,127.0.0.3:18083/api/judge");
    oj::dispatcher::RoundRobinSelector selector{endpoints};

    ASSERT_EQ(selector.size(), 3u);
    EXPECT_EQ(selector.next().port, 18081);
    EXPECT_EQ(selector.next().port, 18082);
    EXPECT_EQ(selector.next().port, 18083);
    EXPECT_EQ(selector.next().port, 18081);
    EXPECT_EQ(selector.next().port, 18082);
}

TEST(DispatcherUtilsTest, WorkerEndpointConfigurationParsingSupportsPathAndDefaultPath) {
    const auto endpoints = oj::dispatcher::parse_worker_endpoints(
        "http://worker-a:19000/custom/judge, worker-b:19001");

    ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_STREQ(endpoints[0].host, "worker-a");
    EXPECT_EQ(endpoints[0].port, 19000);
    EXPECT_STREQ(endpoints[0].judge_api_path, "/custom/judge");

    EXPECT_STREQ(endpoints[1].host, "worker-b");
    EXPECT_EQ(endpoints[1].port, 19001);
    EXPECT_STREQ(endpoints[1].judge_api_path, "/api/judge");
}

} // namespace