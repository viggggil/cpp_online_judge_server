#include "services/judge_worker/compile_service.h"
#include "tests/test_support/temp_dir.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

TEST(CompileServiceTest, ValidCppSourceProducesExecutable) {
  oj::test::TempDirectory temp{"oj_compile_service_test_"};

  const std::string source =
      "#include <iostream>\n"
      "int main() {\n"
      "  std::cout << 42 << '\\n';\n"
      "  return 0;\n"
      "}\n";

  oj::worker::CompileService service;
  const auto result = service.compile(temp.path(), "cpp", source);

  ASSERT_TRUE(result.success) << result.stderr_text;
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(result.source_path));
  EXPECT_TRUE(std::filesystem::exists(result.executable_path));
  EXPECT_NE(result.command.find("g++"), std::string::npos);
}

TEST(CompileServiceTest, InvalidCppSourceReturnsCompileFailure) {
  oj::test::TempDirectory temp{"oj_compile_service_test_"};

  const std::string source =
      "#include <iostream>\n"
      "int main( {\n"
      "  return 0;\n"
      "}\n";

  oj::worker::CompileService service;
  const auto result = service.compile(temp.path(), "cpp", source);

  EXPECT_FALSE(result.success);
  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(result.source_path));
  EXPECT_FALSE(std::filesystem::exists(result.executable_path));
  EXPECT_FALSE(result.stderr_text.empty());
  EXPECT_NE(result.stderr_text.find("error:"), std::string::npos);
  EXPECT_EQ(result.stderr_text.find("Resource temporarily unavailable"), std::string::npos);
}

}  // namespace
