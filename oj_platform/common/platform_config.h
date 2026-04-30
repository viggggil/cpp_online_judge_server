#pragma once

#include <cstdlib>
#include <string>

namespace oj::common {

inline const char* env_or_default(const char* env_name, const char* default_value) {
    if (const char* value = std::getenv(env_name); value != nullptr && *value != '\0') {
        return value;
    }
    return default_value;
}

inline int env_int_or_default(const char* env_name, int default_value) {
    if (const char* value = std::getenv(env_name); value != nullptr && *value != '\0') {
        try {
            return std::stoi(value);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

struct ServiceConfig {
    int port{};
    const char* service_name{};
};

struct AuthConfig {
    const char* admin_register_code{env_or_default("OJ_ADMIN_REGISTER_CODE", "")};
};

struct RedisConfig {
    const char* host{env_or_default("OJ_REDIS_HOST", "127.0.0.1")};
    int port{env_int_or_default("OJ_REDIS_PORT", 6379)};
    int db{env_int_or_default("OJ_REDIS_DB", 0)};
    const char* password{env_or_default("OJ_REDIS_PASSWORD", "")};
    int socket_timeout_ms{env_int_or_default("OJ_REDIS_TIMEOUT_MS", 5000)};
    long long problem_list_ttl_seconds{env_int_or_default("OJ_PROBLEM_LIST_TTL_SECONDS", 60)};
    const char* submission_queue_key{env_or_default("OJ_SUBMISSION_QUEUE_KEY", "oj:queue:submissions")};
};

struct MySqlConfig {
    const char* host{env_or_default("OJ_MYSQL_HOST", "127.0.0.1")};
    int port{env_int_or_default("OJ_MYSQL_PORT", 3306)};
    const char* username{env_or_default("OJ_MYSQL_USER", "oj")};
    const char* password{env_or_default("OJ_MYSQL_PASSWORD", "oj123456")};
    const char* database{env_or_default("OJ_MYSQL_DATABASE", "oj_platform")};
    const char* charset{env_or_default("OJ_MYSQL_CHARSET", "utf8mb4")};
    bool auto_reconnect{true};
};

struct JudgeWorkerEndpoint {
    const char* host{"127.0.0.1"};
    int port{18081};
    const char* judge_api_path{"/api/judge"};
    int connect_timeout_ms{3000};
    int read_timeout_ms{30000};
};

} // namespace oj::common
