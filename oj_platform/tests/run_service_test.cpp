#include "services/judge_worker/compile_service.h"
#include "services/judge_worker/run_service.h"
#include "tests/test_support/temp_dir.h"

#include <gtest/gtest.h>

namespace {

oj::worker::CompileResult compile_fixture(const std::filesystem::path& work_dir,
                                          const std::string& source) {
  oj::worker::CompileService compile_service;
  return compile_service.compile(work_dir, "cpp", source);
}

TEST(RunServiceTest, RunsProgramWithStandardInputAndCapturesOutput) {
  oj::test::TempDirectory temp{"oj_run_service_test_"};

  const auto compile_result = compile_fixture(
      temp.path() / "compile",
      "#include <iostream>\n"
      "int main() {\n"
      "  long long a = 0;\n"
      "  long long b = 0;\n"
      "  std::cin >> a >> b;\n"
      "  std::cout << (a + b) << '\\n';\n"
      "  return 0;\n"
      "}\n");
  ASSERT_TRUE(compile_result.success) << compile_result.stderr_text;

  oj::worker::RunService run_service;
  const auto result = run_service.run(
      compile_result.executable_path,
      "20 22\n",
      1000,
      128,
      temp.path() / "run_ok");

  EXPECT_EQ(result.status, oj::protocol::JudgeStatus::ok);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.stdout_text, "42\n");
  EXPECT_FALSE(result.timed_out);
}

TEST(RunServiceTest, NonZeroExitCodeBecomesRuntimeError) {
  oj::test::TempDirectory temp{"oj_run_service_test_"};

  const auto compile_result = compile_fixture(
      temp.path() / "compile",
      "int main() {\n"
      "  return 7;\n"
      "}\n");
  ASSERT_TRUE(compile_result.success) << compile_result.stderr_text;

  oj::worker::RunService run_service;
  const auto result = run_service.run(
      compile_result.executable_path,
      "",
      1000,
      128,
      temp.path() / "run_re");

  EXPECT_EQ(result.status, oj::protocol::JudgeStatus::runtime_error);
  EXPECT_EQ(result.exit_code, 7);
  EXPECT_NE(result.error_message.find("non-zero"), std::string::npos);
}

TEST(RunServiceTest, LongRunningProgramBecomesTimeLimitExceeded) {
  oj::test::TempDirectory temp{"oj_run_service_test_"};

  const auto compile_result = compile_fixture(
      temp.path() / "compile",
      "int main() {\n"
      "  while (true) {}\n"
      "  return 0;\n"
      "}\n");
  ASSERT_TRUE(compile_result.success) << compile_result.stderr_text;

  oj::worker::RunService run_service;
  const auto result = run_service.run(
      compile_result.executable_path,
      "",
      50,
      128,
      temp.path() / "run_tle");

  EXPECT_EQ(result.status, oj::protocol::JudgeStatus::time_limit_exceeded);
  EXPECT_TRUE(result.timed_out);
}

}  // namespace
