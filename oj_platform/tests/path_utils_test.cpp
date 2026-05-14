#include "common/path_utils.h"
#include "tests/test_support/temp_dir.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

TEST(PathUtilsTest, ResolveExistingRelativePathFromCurrentDirectory) {
  oj::test::TempDirectory temp{"oj_path_utils_test_"};
  oj::test::ScopedCurrentPath scoped_path{temp.path()};

  const auto file_path = temp.path() / "data" / "sample.txt";
  oj::test::write_text_file(file_path, "hello");

  const auto resolved = oj::common::resolve_project_path("data/sample.txt");

  EXPECT_TRUE(std::filesystem::exists(resolved));
  EXPECT_EQ(std::filesystem::weakly_canonical(resolved),
            std::filesystem::weakly_canonical(file_path));
}

TEST(PathUtilsTest, ResolveMissingRelativePathFallsBackToCurrentDirectory) {
  oj::test::TempDirectory temp{"oj_path_utils_test_"};
  oj::test::ScopedCurrentPath scoped_path{temp.path()};

  const auto resolved = oj::common::resolve_project_path("missing/file.txt");

  EXPECT_EQ(resolved, temp.path() / "missing/file.txt");
}

TEST(PathUtilsTest, ExecutableDirIsNotEmpty) {
  EXPECT_FALSE(oj::common::executable_dir().empty());
}

}  // namespace
