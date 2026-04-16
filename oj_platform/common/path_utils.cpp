#include "common/path_utils.h"

#include <array>
#include <system_error>
#include <unistd.h>

namespace oj::common {

std::filesystem::path executable_path() {
    std::array<char, 4096> buffer{};
    const auto size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size <= 0) {
        return {};
    }
    buffer[static_cast<std::size_t>(size)] = '\0';
    return std::filesystem::path{buffer.data()};
}

std::filesystem::path executable_dir() {
    const auto path = executable_path();
    return path.empty() ? std::filesystem::current_path() : path.parent_path();
}

std::filesystem::path resolve_project_path(const std::filesystem::path& relative_path) {
    if (relative_path.is_absolute() && std::filesystem::exists(relative_path)) {
        return relative_path;
    }

    const auto cwd = std::filesystem::current_path();
    const auto exe_dir = executable_dir();

    const std::vector<std::filesystem::path> candidates = {
        relative_path,
        cwd / "oj_platform" / relative_path,
        exe_dir.parent_path() / relative_path,
        exe_dir.parent_path() / "oj_platform" / relative_path,
        exe_dir.parent_path().parent_path() / relative_path,
        exe_dir.parent_path().parent_path() / "oj_platform" / relative_path,
        cwd / relative_path,
        exe_dir / relative_path,
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }

    return std::filesystem::current_path() / relative_path;
}

} // namespace oj::common