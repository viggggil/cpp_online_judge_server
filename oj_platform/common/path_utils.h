#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace oj::common {

std::filesystem::path executable_path();
std::filesystem::path executable_dir();
std::filesystem::path resolve_project_path(const std::filesystem::path& relative_path);

} // namespace oj::common