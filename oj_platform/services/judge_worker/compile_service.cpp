#include "services/judge_worker/compile_service.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

bool write_text_file(const std::filesystem::path& file_path, const std::string& content) {
    std::ofstream output(file_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << content;
    return output.good();
}

int open_redirect_file(const std::filesystem::path& path) {
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

void set_limit(int resource, rlim_t soft, rlim_t hard) {
    struct rlimit limit {
        soft,
        hard,
    };
    (void)::setrlimit(resource, &limit);
}

} // namespace

// 在受限子进程中完成源码落盘、编译执行以及编译日志采集。
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
        if (!write_text_file(result.source_path, source_code)) {
            throw std::runtime_error("failed to open source file for writing: " + result.source_path.string());
        }
    }

    result.command = build_compile_command(
        result.source_path, result.executable_path, language, stdout_path, stderr_path);

    pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed for compiler process");
    }

    if (pid == 0) {
        const int stdout_fd = open_redirect_file(stdout_path);
        const int stderr_fd = open_redirect_file(stderr_path);
        if (stdout_fd < 0 || stderr_fd < 0) {
            _exit(127);
        }

        if (::dup2(stdout_fd, STDOUT_FILENO) < 0 || ::dup2(stderr_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        ::close(stdout_fd);
        ::close(stderr_fd);

        set_limit(RLIMIT_CPU, 10, 10);
        set_limit(RLIMIT_AS, static_cast<rlim_t>(1024) * 1024 * 1024, static_cast<rlim_t>(1024) * 1024 * 1024);
        set_limit(RLIMIT_FSIZE, static_cast<rlim_t>(64) * 1024 * 1024, static_cast<rlim_t>(64) * 1024 * 1024);
        set_limit(RLIMIT_NPROC, 32, 32);
        set_limit(RLIMIT_NOFILE, 64, 64);

        if (language == "cpp") {
            const std::string source = result.source_path.string();
            const std::string executable = result.executable_path.string();
            ::execlp("g++",
                     "g++",
                     "-std=c++17",
                     "-O2",
                     "-pipe",
                     source.c_str(),
                     "-o",
                     executable.c_str(),
                     static_cast<char*>(nullptr));
        }
        _exit(127);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        throw std::runtime_error("waitpid failed for compiler process");
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }
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

// 生成与真实执行参数一致的编译命令字符串，便于日志记录和问题排查。
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
