#include "common/platform_config.h"
#include "tests/test_support/env_guard.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace {

TEST(PlatformConfigTest, EnvOrDefaultReturnsDefaultWhenUnset) {
  oj::test::EnvGuard guard{"OJ_TEST_STRING_VALUE"};
  ::unsetenv("OJ_TEST_STRING_VALUE");

  EXPECT_STREQ(
      oj::common::env_or_default("OJ_TEST_STRING_VALUE", "fallback"),
      "fallback");
}

TEST(PlatformConfigTest, EnvOrDefaultReturnsDefaultWhenEmpty) {
  oj::test::EnvGuard guard{"OJ_TEST_STRING_VALUE"};
  ::setenv("OJ_TEST_STRING_VALUE", "", 1);

  EXPECT_STREQ(
      oj::common::env_or_default("OJ_TEST_STRING_VALUE", "fallback"),
      "fallback");
}

TEST(PlatformConfigTest, EnvOrDefaultReturnsConfiguredValue) {
  oj::test::EnvGuard guard{"OJ_TEST_STRING_VALUE"};
  ::setenv("OJ_TEST_STRING_VALUE", "configured", 1);

  EXPECT_STREQ(
      oj::common::env_or_default("OJ_TEST_STRING_VALUE", "fallback"),
      "configured");
}

TEST(PlatformConfigTest, EnvIntOrDefaultReturnsDefaultWhenUnset) {
  oj::test::EnvGuard guard{"OJ_TEST_INT_VALUE"};
  ::unsetenv("OJ_TEST_INT_VALUE");

  EXPECT_EQ(
      oj::common::env_int_or_default("OJ_TEST_INT_VALUE", 1234),
      1234);
}

TEST(PlatformConfigTest, EnvIntOrDefaultReturnsConfiguredInteger) {
  oj::test::EnvGuard guard{"OJ_TEST_INT_VALUE"};
  ::setenv("OJ_TEST_INT_VALUE", "5678", 1);

  EXPECT_EQ(
      oj::common::env_int_or_default("OJ_TEST_INT_VALUE", 1234),
      5678);
}

TEST(PlatformConfigTest, EnvIntOrDefaultReturnsDefaultWhenInvalid) {
  oj::test::EnvGuard guard{"OJ_TEST_INT_VALUE"};
  ::setenv("OJ_TEST_INT_VALUE", "not-a-number", 1);

  EXPECT_EQ(
      oj::common::env_int_or_default("OJ_TEST_INT_VALUE", 1234),
      1234);
}

}  // namespace
