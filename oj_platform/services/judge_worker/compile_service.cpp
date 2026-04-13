#include "services/judge_worker/compile_service.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace oj::worker {

namespace {

std::string read_text_file(const std::filesystem::path& file_path) {
    std::ifstream input(file_path, std::ios::in | std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string shell_escape(const std::filesystem::path& value) {
    const auto raw = value.string();
    std::string escaped;
    escaped.reserve(raw.size() + 2);
    escaped.push_back('\'');
    for (char ch : raw) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

} // namespace

CompileResult CompileService::compile(const std::filesystem::path& work_directory,
                                      const std::string& language,
                                      const std::string& source_code) const {
    std::filesystem::create_directories(work_directory);

    CompileResult result;
    result.source_path = resolve_source_path(work_directory, language);
    result.executable_path = resolve_executable_path(work_directory);

    const auto stdout_path = work_directory / "compile.stdout.log";
    const auto stderr_path = work_directory / "compile.stderr.log";

    {
        std::ofstream output(result.source_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("failed to open source file for writing: " + result.source_path.string());
        }
        output << source_code;
    }

    result.command = build_compile_command(
        result.source_path, result.executable_path, language, stdout_path, stderr_path);
    result.exit_code = std::system(result.command.c_str());
    result.stdout_text = read_text_file(stdout_path);
    result.stderr_text = read_text_file(stderr_path);
    result.success = (result.exit_code == 0) && std::filesystem::exists(result.executable_path);

    return result;
}

std::filesystem::path CompileService::resolve_source_path(const std::filesystem::path& work_directory,
                                                          const std::string& language) const {
    if (language == "cpp") {
        return work_directory / "main.cpp";
    }

    throw std::runtime_error("unsupported language for compilation: " + language);
}

std::filesystem::path CompileService::resolve_executable_path(const std::filesystem::path& work_directory) const {
    return work_directory / "main.bin";
}

std::string CompileService::build_compile_command(const std::filesystem::path& source_path,
                                                  const std::filesystem::path& executable_path,
                                                  const std::string& language,
                                                  const std::filesystem::path& stdout_path,
                                                  const std::filesystem::path& stderr_path) const {
    if (language == "cpp") {
        return "g++ -std=c++17 -O2 -pipe " + shell_escape(source_path) +
               " -o " + shell_escape(executable_path) +
               " 1>" + shell_escape(stdout_path) +
               " 2>" + shell_escape(stderr_path);
    }

    throw std::runtime_error("unsupported language for compilation: " + language);
}

} // namespace oj::worker

