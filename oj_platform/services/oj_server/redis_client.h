#pragma once

#include "common/platform_config.h"

#include <optional>
#include <string>

namespace sw::redis {
class Redis;
}

namespace oj::server {

class RedisClient {
public:
    explicit RedisClient(const oj::common::RedisConfig& config = {});
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;
    RedisClient(RedisClient&&) noexcept;
    RedisClient& operator=(RedisClient&&) noexcept;

    bool available() const noexcept;
    std::optional<std::string> get(const std::string& key) const;
    bool setex(const std::string& key, long long ttl_seconds, const std::string& value) const;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace oj::server