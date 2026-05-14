#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

namespace oj::test {

inline std::string unique_suffix() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::random_device rd;
  std::mt19937_64 rng(rd());
  return std::to_string(now) + "_" + std::to_string(rng());
}

class TempDirectory {
public:
  explicit TempDirectory(const std::string& prefix = "oj_test_")
      : path_(std::filesystem::temp_directory_path() / (prefix + unique_suffix())) {
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const {
    return path_;
  }

private:
  std::filesystem::path path_;
};

class ScopedCurrentPath {
public:
  explicit ScopedCurrentPath(const std::filesystem::path& path)
      : original_path_(std::filesystem::current_path()) {
    std::filesystem::current_path(path);
  }

  ~ScopedCurrentPath() {
    std::error_code ignored;
    std::filesystem::current_path(original_path_, ignored);
  }

private:
  std::filesystem::path original_path_;
};

inline void write_text_file(const std::filesystem::path& path,
                            const std::string& content) {
  std::filesystem::create_directories(path.parent_path());

  std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("failed to write file: " + path.string());
  }

  output << content;

  if (!output.good()) {
    throw std::runtime_error("failed to flush file: " + path.string());
  }
}

}  // namespace oj::test
