#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace oj::protocol {

// OJ Server <-> Judge Worker 之间共享的公共协议定义。
// 这一层只描述“请求/响应的数据长什么样”，不耦合具体 HTTP / JSON 框架。

enum class JudgeStatus : std::uint8_t {
    ok = 0,
    compile_error,
    runtime_error,
    time_limit_exceeded,
    memory_limit_exceeded,
    output_limit_exceeded,
    wrong_answer,
    presentation_error,
    system_error,
};

inline constexpr std::string_view to_string(JudgeStatus status) noexcept {
    switch (status) {
        case JudgeStatus::ok:
            return "OK";
        case JudgeStatus::compile_error:
            return "COMPILE_ERROR";
        case JudgeStatus::runtime_error:
            return "RUNTIME_ERROR";
        case JudgeStatus::time_limit_exceeded:
            return "TIME_LIMIT_EXCEEDED";
        case JudgeStatus::memory_limit_exceeded:
            return "MEMORY_LIMIT_EXCEEDED";
        case JudgeStatus::output_limit_exceeded:
            return "OUTPUT_LIMIT_EXCEEDED";
        case JudgeStatus::wrong_answer:
            return "WRONG_ANSWER";
        case JudgeStatus::presentation_error:
            return "PRESENTATION_ERROR";
        case JudgeStatus::system_error:
            return "SYSTEM_ERROR";
    }
    return "UNKNOWN";
}

enum class LanguageType : std::uint8_t {
    cpp = 0,
    // 预留扩展：c, java, python, go ...
};

inline constexpr std::string_view to_string(LanguageType language) noexcept {
    switch (language) {
        case LanguageType::cpp:
            return "cpp";
    }
    return "unknown";
}

// 单个测试点，由 OJ Server 下发给 Judge Worker。
struct TestCase {
    std::string input;
    std::string expected_output;

    std::string input_object_key;
    std::string output_object_key;

    std::string input_sha256;
    std::string output_sha256;

    std::int64_t input_size_bytes{0};
    std::int64_t output_size_bytes{0};
};

// 单个测试点的执行结果，由 Worker 返回。
struct TestCaseResult {
    JudgeStatus status{JudgeStatus::system_error};
    std::int32_t time_used_ms{0};
    std::int32_t memory_used_kb{0};
    std::string input;
    std::string actual_output;
    std::string expected_output;
    std::string error_message;
};

// OJ Server -> Judge Worker
struct JudgeRequest {
    std::int64_t submission_id{0};
    std::int64_t problem_id{0};
    LanguageType language{LanguageType::cpp};
    std::string source_code;

    std::int32_t time_limit_ms{1000};
    std::int32_t memory_limit_mb{128};

    std::vector<TestCase> test_cases;
};

// Judge Worker -> OJ Server
struct JudgeResponse {
    std::int64_t submission_id{0};
    JudgeStatus final_status{JudgeStatus::system_error};

    // 编译阶段信息
    bool compile_success{false};
    std::string compile_stdout;
    std::string compile_stderr;

    // 运行阶段汇总
    std::int32_t total_time_used_ms{0};
    std::int32_t peak_memory_used_kb{0};

    // 每个测试点结果
    std::vector<TestCaseResult> test_case_results;

    // 系统级错误说明，例如沙箱初始化失败、文件系统异常等
    std::string system_message;
};

// 面向 OJ Server 对外 API 的题目元信息。
struct ProblemMeta {
    std::int64_t id{0};
    std::string title;
    std::int32_t time_limit_ms{1000};
    std::int32_t memory_limit_mb{128};
    std::vector<std::string> tags;
};

// 面向 OJ Server 对外 API 的题目详情。
struct ProblemDetail {
    std::int64_t id{0};
    std::string title;
    std::string statement_markdown;
    std::int32_t time_limit_ms{1000};
    std::int32_t memory_limit_mb{128};
    std::vector<std::string> tags;
};

} // namespace oj::protocol
