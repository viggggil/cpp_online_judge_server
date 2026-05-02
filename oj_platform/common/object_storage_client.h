#pragma once

#include "common/platform_config.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace oj::common {

class ObjectStorageClient {
public:
    explicit ObjectStorageClient(ObjectStorageConfig config = ObjectStorageConfig{});

    void ensure_alias() const;
    void ensure_bucket() const;
    void upload_file(const std::filesystem::path& local_path, const std::string& object_key) const;
    void download_file(const std::string& object_key, const std::filesystem::path& local_path) const;
    void delete_object(const std::string& object_key) const;

private:
    std::string object_uri(const std::string& object_key) const;

    ObjectStorageConfig config_;
};

std::string sha256_file(const std::filesystem::path& path);
std::int64_t file_size_bytes(const std::filesystem::path& path);
std::string read_text_file(const std::filesystem::path& path);

} // namespace oj::common
