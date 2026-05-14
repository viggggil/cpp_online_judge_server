#include "common/protocol_json.h"
#include "services/judge_worker/judge_core.h"
#include "services/judge_worker/routes.h"
#include "tests/test_support/http_client.h"
#include "tests/test_support/temp_dir.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <thread>

#include "crow.h"

namespace {

int integration_test_port() {
  return 19081 + static_cast<int>(::getpid() % 1000);
}

oj::protocol::JudgeRequest make_sum_request(std::int64_t submission_id) {
  oj::protocol::JudgeRequest request;
  request.submission_id = submission_id;
  request.problem_id = 1001;
  request.language = oj::protocol::LanguageType::cpp;
  request.source_code =
      "#include <iostream>\n"
      "int main() {\n"
      "  long long a = 0;\n"
      "  long long b = 0;\n"
      "  std::cin >> a >> b;\n"
      "  std::cout << (a + b) << '\\n';\n"
      "  return 0;\n"
      "}\n";
  request.time_limit_ms = 1000;
  request.memory_limit_mb = 128;
  request.test_cases = {
      {"1 2\n", "3\n"},
      {"20 22\n", "42\n"},
  };
  return request;
}

class CrowServerGuard {
public:
  explicit CrowServerGuard(crow::SimpleApp& app, int port) : app_(app) {
    server_thread_ = std::thread([this, port] {
      app_.bindaddr("127.0.0.1").port(port).run();
    });
  }

  ~CrowServerGuard() {
    app_.stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  CrowServerGuard(const CrowServerGuard&) = delete;
  CrowServerGuard& operator=(const CrowServerGuard&) = delete;

private:
  crow::SimpleApp& app_;
  std::thread server_thread_;
};

void wait_until_server_ready(int port) {
  for (int i = 0; i < 50; ++i) {
    try {
      const auto response = oj::test::http_post_json(
          "127.0.0.1",
          port,
          "/api/judge",
          "{invalid-json");

      if (response.status_code == 400 || response.status_code == 500) {
        return;
      }
    } catch (...) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  throw std::runtime_error("judge_worker test server did not become ready");
}

TEST(JudgeWorkerHttpIntegrationTest, JudgeApiReturnsOkForValidCppSubmission) {
  oj::test::TempDirectory temp{"oj_worker_http_integration_test_"};
  oj::test::ScopedCurrentPath scoped_path{temp.path()};

  const int port = integration_test_port();

  auto judge_core = std::make_shared<oj::worker::JudgeCore>();
  oj::worker::WorkerAppContext context{
      oj::common::ServiceConfig{port, "judge_worker_test"},
      judge_core,
  };

  crow::SimpleApp app;
  oj::worker::register_routes(app, context);

  CrowServerGuard server{app, port};
  wait_until_server_ready(port);

  const auto request = make_sum_request(90001);
  const auto request_body = oj::common::serialize_judge_request(request);

  const auto http_response = oj::test::http_post_json(
      "127.0.0.1",
      port,
      "/api/judge",
      request_body);

  ASSERT_EQ(http_response.status_code, 200) << http_response.raw;

  const auto judge_response =
      oj::common::deserialize_judge_response(http_response.body);

  EXPECT_EQ(judge_response.submission_id, request.submission_id);
  EXPECT_TRUE(judge_response.compile_success) << judge_response.compile_stderr;
  EXPECT_EQ(judge_response.final_status, oj::protocol::JudgeStatus::ok);
  ASSERT_EQ(judge_response.test_case_results.size(), 2u);
  EXPECT_EQ(judge_response.test_case_results[0].actual_output, "3\n");
  EXPECT_EQ(judge_response.test_case_results[1].actual_output, "42\n");
}

TEST(JudgeWorkerHttpIntegrationTest, JudgeApiReturnsBadRequestForInvalidJson) {
  oj::test::TempDirectory temp{"oj_worker_http_integration_test_"};
  oj::test::ScopedCurrentPath scoped_path{temp.path()};

  const int port = integration_test_port() + 1;

  auto judge_core = std::make_shared<oj::worker::JudgeCore>();
  oj::worker::WorkerAppContext context{
      oj::common::ServiceConfig{port, "judge_worker_test"},
      judge_core,
  };

  crow::SimpleApp app;
  oj::worker::register_routes(app, context);

  CrowServerGuard server{app, port};
  wait_until_server_ready(port);

  const auto http_response = oj::test::http_post_json(
      "127.0.0.1",
      port,
      "/api/judge",
      "{invalid-json");

  EXPECT_EQ(http_response.status_code, 400);
  EXPECT_NE(http_response.body.find("error"), std::string::npos);
}

}  // namespace
