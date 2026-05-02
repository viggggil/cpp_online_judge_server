#include "common/object_storage_client.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace oj::common {
namespace {

std::string shell_quote(const std::string& value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');

    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }

    quoted.push_back('\'');
    return quoted;
}

std::string shell_quote(const std::filesystem::path& path) {
    return shell_quote(path.string());
}

void run_command(const std::string& command, const std::string& error_message) {
    const int code = std::system(command.c_str());
    if (code != 0) {
        throw std::runtime_error(error_message + ", command: " + command);
    }
}

std::string read_command_output(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("failed to run command: " + command);
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

    const int code = pclose(pipe);
    if (code != 0) {
        throw std::runtime_error("command failed: " + command);
    }

    return result;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

} // namespace

ObjectStorageClient::ObjectStorageClient(ObjectStorageConfig config)
    : config_(config) {}

void ObjectStorageClient::ensure_alias() const {
    const std::string command =
        "mc alias set " +
        shell_quote(std::string{config_.alias}) + " " +
        shell_quote(std::string{config_.endpoint}) + " " +
        shell_quote(std::string{config_.access_key}) + " " +
        shell_quote(std::string{config_.secret_key}) + " >/dev/null";

    run_command(command, "failed to configure minio alias");
}

void ObjectStorageClient::ensure_bucket() const {
    ensure_alias();

    const std::string command =
        "mc mb --ignore-existing " +
        shell_quote(std::string(config_.alias) + "/" + std::string{config_.bucket}) +
        " >/dev/null";

    run_command(command, "failed to ensure minio bucket");
}

std::string ObjectStorageClient::object_uri(const std::string& object_key) const {
    if (object_key.empty()) {
        throw std::runtime_error("object key is empty");
    }

    if (!object_key.empty() && object_key.front() == '/') {
        throw std::runtime_error("object key must not start with /");
    }

    if (object_key.find("..") != std::string::npos) {
        throw std::runtime_error("object key must not contain ..");
    }

    return std::string(config_.alias) + "/" + std::string{config_.bucket} + "/" + object_key;
}

void ObjectStorageClient::upload_file(
    const std::filesystem::path& local_path,
    const std::string& object_key) const {
    if (!std::filesystem::is_regular_file(local_path)) {
        throw std::runtime_error("upload source is not a regular file: " + local_path.string());
    }

    ensure_bucket();

    const std::string command =
        "mc cp " +
        shell_quote(local_path) + " " +
        shell_quote(object_uri(object_key)) +
        " >/dev/null";

    run_command(command, "failed to upload object to minio");
}

void ObjectStorageClient::download_file(
    const std::string& object_key,
    const std::filesystem::path& local_path) const {
    ensure_bucket();

    std::filesystem::create_directories(local_path.parent_path());

    const std::string command =
        "mc cp " +
        shell_quote(object_uri(object_key)) + " " +
        shell_quote(local_path) +
        " >/dev/null";

    run_command(command, "failed to download object from minio");
}

void ObjectStorageClient::delete_object(const std::string& object_key) const {
    ensure_bucket();

    const std::string command =
        "mc rm --force " +
        shell_quote(object_uri(object_key)) +
        " >/dev/null";

    run_command(command, "failed to delete object from minio");
}

std::string sha256_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("sha256 source is not a regular file: " + path.string());
    }

    const std::string command =
        "sha256sum " + shell_quote(path) + " | awk '{print $1}'";

    return trim(read_command_output(command));
}

std::int64_t file_size_bytes(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("file size source is not a regular file: " + path.string());
    }

    return static_cast<std::int64_t>(std::filesystem::file_size(path));
}

std::string read_text_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("read source is not a regular file: " + path.string());
    }

    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace oj::common
