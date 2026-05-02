#include "common/protocol_json.h"

#include <gtest/gtest.h>

namespace {

TEST(ProtocolJsonTest, JudgeRequestSerializeDeserializeRoundTrip) {
    oj::protocol::JudgeRequest request;
    request.submission_id = 12345;
    request.problem_id = 1001;
    request.language = oj::protocol::LanguageType::cpp;
    request.source_code = "#include <iostream>\nint main(){std::cout<<42<<'\\n';}";
    request.time_limit_ms = 1500;
    request.memory_limit_mb = 256;
    request.test_cases = {
        {"1 2\n", "3\n", "objects/1.in", "objects/1.out", "sha-in-1", "sha-out-1", 4, 2},
        {"2 5\n", "7\n", "objects/2.in", "objects/2.out", "sha-in-2", "sha-out-2", 4, 2},
    };

    const auto json = oj::common::serialize_judge_request(request);
    const auto parsed = oj::common::deserialize_judge_request(json);

    EXPECT_EQ(parsed.submission_id, request.submission_id);
    EXPECT_EQ(parsed.problem_id, request.problem_id);
    EXPECT_EQ(parsed.language, request.language);
    EXPECT_EQ(parsed.source_code, request.source_code);
    EXPECT_EQ(parsed.time_limit_ms, request.time_limit_ms);
    EXPECT_EQ(parsed.memory_limit_mb, request.memory_limit_mb);
    ASSERT_EQ(parsed.test_cases.size(), request.test_cases.size());
    EXPECT_EQ(parsed.test_cases[0].input, "1 2\n");
    EXPECT_EQ(parsed.test_cases[0].expected_output, "3\n");
    EXPECT_EQ(parsed.test_cases[0].input_object_key, "objects/1.in");
    EXPECT_EQ(parsed.test_cases[0].output_object_key, "objects/1.out");
    EXPECT_EQ(parsed.test_cases[0].input_sha256, "sha-in-1");
    EXPECT_EQ(parsed.test_cases[0].output_sha256, "sha-out-1");
    EXPECT_EQ(parsed.test_cases[0].input_size_bytes, 4);
    EXPECT_EQ(parsed.test_cases[0].output_size_bytes, 2);
    EXPECT_EQ(parsed.test_cases[1].input, "2 5\n");
    EXPECT_EQ(parsed.test_cases[1].expected_output, "7\n");
    EXPECT_EQ(parsed.test_cases[1].input_object_key, "objects/2.in");
    EXPECT_EQ(parsed.test_cases[1].output_object_key, "objects/2.out");
}

TEST(ProtocolJsonTest, JudgeResponseSerializeDeserializeRoundTrip) {
    oj::protocol::JudgeResponse response;
    response.submission_id = 7788;
    response.final_status = oj::protocol::JudgeStatus::wrong_answer;
    response.compile_success = true;
    response.compile_stdout = "compiled";
    response.compile_stderr = "warning";
    response.total_time_used_ms = 37;
    response.peak_memory_used_kb = 4096;
    response.system_message = "";
    response.test_case_results = {
        {oj::protocol::JudgeStatus::ok, 10, 1024, "1 2\n", "3\n", "3\n", ""},
        {oj::protocol::JudgeStatus::wrong_answer, 27, 4096, "2 2\n", "5\n", "4\n", "mismatch"},
    };

    const auto json = oj::common::serialize_judge_response(response);
    const auto parsed = oj::common::deserialize_judge_response(json);

    EXPECT_EQ(parsed.submission_id, response.submission_id);
    EXPECT_EQ(parsed.final_status, response.final_status);
    EXPECT_EQ(parsed.compile_success, response.compile_success);
    EXPECT_EQ(parsed.compile_stdout, response.compile_stdout);
    EXPECT_EQ(parsed.compile_stderr, response.compile_stderr);
    EXPECT_EQ(parsed.total_time_used_ms, response.total_time_used_ms);
    EXPECT_EQ(parsed.peak_memory_used_kb, response.peak_memory_used_kb);
    ASSERT_EQ(parsed.test_case_results.size(), 2u);
    EXPECT_EQ(parsed.test_case_results[0].status, oj::protocol::JudgeStatus::ok);
    EXPECT_EQ(parsed.test_case_results[1].status, oj::protocol::JudgeStatus::wrong_answer);
    EXPECT_EQ(parsed.test_case_results[1].actual_output, "5\n");
    EXPECT_EQ(parsed.test_case_results[1].expected_output, "4\n");
    EXPECT_EQ(parsed.test_case_results[1].error_message, "mismatch");
}

TEST(ProtocolJsonTest, JudgeResponseSupportsOutputLimitExceeded) {
    oj::protocol::JudgeResponse response;
    response.submission_id = 9001;
    response.final_status = oj::protocol::JudgeStatus::output_limit_exceeded;
    response.compile_success = true;
    response.test_case_results = {
        {oj::protocol::JudgeStatus::output_limit_exceeded, 100, 2048, "", std::string(32, 'a'), "", "too much output"},
    };

    const auto json = oj::common::serialize_judge_response(response);
    const auto parsed = oj::common::deserialize_judge_response(json);

    EXPECT_EQ(parsed.final_status, oj::protocol::JudgeStatus::output_limit_exceeded);
    ASSERT_EQ(parsed.test_case_results.size(), 1u);
    EXPECT_EQ(parsed.test_case_results[0].status, oj::protocol::JudgeStatus::output_limit_exceeded);
}

} // namespace
