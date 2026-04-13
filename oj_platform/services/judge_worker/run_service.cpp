#include "services/judge_worker/run_service.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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

} // namespace

RunResult RunService::run(const std::filesystem::path& executable_path,
                          const std::string& standard_input,
                          std::int32_t time_limit_ms,
                          std::int32_t memory_limit_mb,
                          const std::filesystem::path& work_directory) const {
    std::filesystem::create_directories(work_directory);

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
        FILE* stdin_file = std::freopen(stdin_path.c_str(), "r", stdin);
        FILE* stdout_file = std::freopen(stdout_path.c_str(), "w", stdout);
        FILE* stderr_file = std::freopen(stderr_path.c_str(), "w", stderr);
        if (!stdin_file || !stdout_file || !stderr_file) {
            _exit(127);
        }

        if (memory_limit_mb > 0) {
            const rlim_t memory_bytes = static_cast<rlim_t>(memory_limit_mb) * 1024 * 1024;
            struct rlimit memory_limit {
                memory_bytes,
                memory_bytes,
            };
            setrlimit(RLIMIT_AS, &memory_limit);
        }

        execl(executable_path.c_str(), executable_path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    struct rusage usage {};
    bool finished = false;

    while (true) {
        const pid_t wait_result = wait4(pid, &status, WNOHANG, &usage);
        if (wait_result == pid) {
            finished = true;
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (time_limit_ms > 0 && elapsed_ms > time_limit_ms) {
            kill(pid, SIGKILL);
            wait4(pid, &status, 0, &usage);
            result.timed_out = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    result.time_used_ms = static_cast<std::int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    result.memory_used_kb = static_cast<std::int32_t>(usage.ru_maxrss);
    result.stdout_text = read_text_file(stdout_path);
    result.stderr_text = read_text_file(stderr_path);

    if (result.timed_out) {
        result.status = oj::protocol::JudgeStatus::time_limit_exceeded;
        result.error_message = "process exceeded time limit";
        return result;
    }

    if (!finished) {
        result.status = oj::protocol::JudgeStatus::system_error;
        result.error_message = "wait4 did not observe child exit";
        return result;
    }

    if (WIFSIGNALED(status)) {
        result.signaled = true;
        result.signal_number = WTERMSIG(status);
        result.status = (result.signal_number == SIGKILL && time_limit_ms > 0 && result.time_used_ms >= time_limit_ms)
                            ? oj::protocol::JudgeStatus::time_limit_exceeded
                            : oj::protocol::JudgeStatus::runtime_error;
        result.error_message = "process terminated by signal " + std::to_string(result.signal_number);
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        if (memory_limit_mb > 0 && result.memory_used_kb > memory_limit_mb * 1024) {
            result.status = oj::protocol::JudgeStatus::memory_limit_exceeded;
            result.error_message = "process exceeded memory limit";
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

