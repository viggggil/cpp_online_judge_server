#pragma once

#include <filesystem>
#include <string>

namespace oj::worker {

struct CompileResult {
    bool success{false};
    int exit_code{-1};
    std::filesystem::path source_path;
    std::filesystem::path executable_path;
    std::string command;
    std::string stdout_text;
    std::string stderr_text;
};

class CompileService {
public:
    CompileResult compile(const std::filesystem::path& work_directory,
                          const std::string& language,
                          const std::string& source_code) const;

private:
    std::filesystem::path resolve_source_path(const std::filesystem::path& work_directory,
                                              const std::string& language) const;
    std::filesystem::path resolve_executable_path(const std::filesystem::path& work_directory) const;
    std::string build_compile_command(const std::filesystem::path& source_path,
                                      const std::filesystem::path& executable_path,
                                      const std::string& language,
                                      const std::filesystem::path& stdout_path,
                                      const std::filesystem::path& stderr_path) const;
};

} // namespace oj::worker
