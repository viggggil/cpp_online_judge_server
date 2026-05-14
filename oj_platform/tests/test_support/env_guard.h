#pragma once

#include <cstdlib>
#include <string>

namespace oj::test {

class EnvGuard {
public:
  explicit EnvGuard(const char* key) : key_(key) {
    const char* old_value = std::getenv(key_);
    if (old_value != nullptr) {
      had_old_value_ = true;
      old_value_ = old_value;
    }
  }

  ~EnvGuard() {
    if (had_old_value_) {
      ::setenv(key_, old_value_.c_str(), 1);
    } else {
      ::unsetenv(key_);
    }
  }

  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;

private:
  const char* key_;
  bool had_old_value_{false};
  std::string old_value_;
};

}  // namespace oj::test
