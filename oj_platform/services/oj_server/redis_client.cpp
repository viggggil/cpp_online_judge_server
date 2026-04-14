#include "services/oj_server/redis_client.h"

#include <sw/redis++/redis++.h>

#include <memory>
#include <utility>

namespace oj::server {

struct RedisClient::Impl {
    std::unique_ptr<sw::redis::Redis> redis;
};

namespace {

sw::redis::ConnectionOptions make_connection_options(const oj::common::RedisConfig& config) {
    sw::redis::ConnectionOptions options;
    options.host = config.host;
    options.port = config.port;
    options.db = config.db;
    if (config.password != nullptr && config.password[0] != '\0') {
        options.password = config.password;
    }
    options.socket_timeout = std::chrono::milliseconds(config.socket_timeout_ms);
    return options;
}

} // namespace

RedisClient::RedisClient(const oj::common::RedisConfig& config)
    : impl_(new Impl{}) {
    try {
        impl_->redis = std::make_unique<sw::redis::Redis>(make_connection_options(config));
        impl_->redis->ping();
    } catch (const sw::redis::Error&) {
        impl_->redis.reset();
    }
}

RedisClient::~RedisClient() {
    delete impl_;
}

RedisClient::RedisClient(RedisClient&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)) {}

RedisClient& RedisClient::operator=(RedisClient&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

bool RedisClient::available() const noexcept {
    return impl_ != nullptr && impl_->redis != nullptr;
}

std::optional<std::string> RedisClient::get(const std::string& key) const {
    if (!available()) {
        return std::nullopt;
    }

    try {
        return impl_->redis->get(key);
    } catch (const sw::redis::Error&) {
        return std::nullopt;
    }
}

bool RedisClient::setex(const std::string& key, long long ttl_seconds, const std::string& value) const {
    if (!available()) {
        return false;
    }

    try {
        impl_->redis->setex(key, ttl_seconds, value);
        return true;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

} // namespace oj::server