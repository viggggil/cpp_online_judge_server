#include "services/judge_worker/run_service.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
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

void write_text_file(const std::filesystem::path& file_path, const std::string& content) {
    std::ofstream output(file_path, std::ios::out | std::ios::binary | std::ios::trunc);
    output << content;
}

std::int32_t read_peak_memory_kb(pid_t pid) {
    std::ifstream input("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view prefix = "VmHWM:";
        if (line.rfind(prefix.data(), 0) == 0) {
            std::istringstream iss(line.substr(prefix.size()));
            std::int32_t value = 0;
            iss >> value;
            return value;
        }
    }
    return 0;
}

void set_limit(int resource, rlim_t soft, rlim_t hard) {
    struct rlimit limit {
        soft,
        hard,
    };
    (void)::setrlimit(resource, &limit);
}

int open_input_file(const std::filesystem::path& path) {
    return ::open(path.c_str(), O_RDONLY);
}

int open_output_file(const std::filesystem::path& path) {
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

// 按信号类型和资源使用情况，把异常退出映射成判题系统可理解的结果状态。
oj::protocol::JudgeStatus map_signal_to_status(int signal_number,
                                               bool timed_out,
                                               std::int32_t memory_limit_mb,
                                               std::int32_t observed_memory_kb) {
    if (timed_out || signal_number == SIGKILL || signal_number == SIGXCPU) {
        return oj::protocol::JudgeStatus::time_limit_exceeded;
    }
    if (signal_number == SIGXFSZ) {
        return oj::protocol::JudgeStatus::output_limit_exceeded;
    }
    if (memory_limit_mb > 0 && observed_memory_kb > memory_limit_mb * 1024) {
        return oj::protocol::JudgeStatus::memory_limit_exceeded;
    }
    return oj::protocol::JudgeStatus::runtime_error;
}

} // namespace

// 在受限子进程中执行选手程序，并采集时间、内存、标准输出和错误信息。
RunResult RunService::run(const std::filesystem::path& executable_path,
                          const std::string& standard_input,
                          std::int32_t time_limit_ms,
                          std::int32_t memory_limit_mb,
                          const std::filesystem::path& work_directory) const {
    std::filesystem::create_directories(work_directory);
    const auto executable_absolute_path = std::filesystem::absolute(executable_path);

    const auto stdin_path = work_directory / "run.stdin.txt";
    const auto stdout_path = work_directory / "run.stdout.txt";
    const auto stderr_path = work_directory / "run.stderr.txt";
    write_text_file(stdin_path, standard_input);

    RunResult result;

    const auto start = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0) {
        result.status = oj::protocol::JudgeStatus::system_error;
        result.error_message = "fork failed";
        return result;
    }

    if (pid == 0) {
        const int stdin_fd = open_input_file(stdin_path);
        const int stdout_fd = open_output_file(stdout_path);
        const int stderr_fd = open_output_file(stderr_path);
        if (stdin_fd < 0 || stdout_fd < 0 || stderr_fd < 0) {
            _exit(127);
        }

        if (::dup2(stdin_fd, STDIN_FILENO) < 0 ||
            ::dup2(stdout_fd, STDOUT_FILENO) < 0 ||
            ::dup2(stderr_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        ::close(stdin_fd);
        ::close(stdout_fd);
        ::close(stderr_fd);

        (void)::chdir(work_directory.c_str());

        const rlim_t cpu_seconds = time_limit_ms > 0 ? static_cast<rlim_t>((time_limit_ms + 999) / 1000 + 1) : 1;
        set_limit(RLIMIT_CPU, cpu_seconds, cpu_seconds);

        if (memory_limit_mb > 0) {
            const rlim_t memory_bytes = static_cast<rlim_t>(memory_limit_mb) * 1024 * 1024;
            set_limit(RLIMIT_AS, memory_bytes, memory_bytes);
        }

        const rlim_t output_limit_bytes = static_cast<rlim_t>(16) * 1024 * 1024;
        set_limit(RLIMIT_FSIZE, output_limit_bytes, output_limit_bytes);
        set_limit(RLIMIT_NPROC, 1, 1);
        set_limit(RLIMIT_NOFILE, 16, 16);

        ::execl(executable_absolute_path.c_str(),
                executable_absolute_path.filename().c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    bool finished = false;
    std::int32_t peak_memory_kb = 0;

    while (true) {
        peak_memory_kb = std::max(peak_memory_kb, read_peak_memory_kb(pid));
        const pid_t wait_result = ::waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            finished = true;
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (time_limit_ms > 0 && elapsed_ms > time_limit_ms) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            result.timed_out = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    result.time_used_ms = static_cast<std::int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    result.memory_used_kb = peak_memory_kb;
    result.stdout_text = read_text_file(stdout_path);
    result.stderr_text = read_text_file(stderr_path);

    if (result.timed_out) {
        result.status = oj::protocol::JudgeStatus::time_limit_exceeded;
        result.error_message = "process exceeded time limit";
        return result;
    }

    if (!finished) {
        result.status = oj::protocol::JudgeStatus::system_error;
        result.error_message = "waitpid did not observe child exit";
        return result;
    }

    if (WIFSIGNALED(status)) {
        result.signaled = true;
        result.signal_number = WTERMSIG(status);
        result.status = map_signal_to_status(result.signal_number,
                                             result.timed_out,
                                             memory_limit_mb,
                                             result.memory_used_kb);
        result.error_message = "process terminated by signal " + std::to_string(result.signal_number);
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        if (memory_limit_mb > 0 && result.memory_used_kb > memory_limit_mb * 1024) {
            result.status = oj::protocol::JudgeStatus::memory_limit_exceeded;
            result.error_message = "process exceeded memory limit";
        } else if (result.exit_code == 127) {
            result.status = oj::protocol::JudgeStatus::runtime_error;
            result.error_message = "failed to exec target program";
        } else if (result.exit_code != 0) {
            result.status = oj::protocol::JudgeStatus::runtime_error;
            result.error_message = "process exited with non-zero code " + std::to_string(result.exit_code);
        } else {
            result.status = oj::protocol::JudgeStatus::ok;
        }
        return result;
    }

    result.status = oj::protocol::JudgeStatus::system_error;
    result.error_message = "unknown child process state";
    return result;
}

} // namespace oj::worker
