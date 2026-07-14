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

struct AiInternalApiConfig {
    const char* internal_token{
        env_or_default(
            "OJ_INTERNAL_API_TOKEN",
            env_or_default("INTERNAL_API_TOKEN", ""))
    };
};

struct AgentServiceConfig {
    const char* base_url{env_or_default("OJ_AGENT_BASE_URL", "http://127.0.0.1:8001")};
    const char* diagnoses_api_path{env_or_default("OJ_AGENT_DIAGNOSES_API_PATH", "/api/v1/diagnoses")};
    const char* internal_token{
        env_or_default(
            "OJ_INTERNAL_API_TOKEN",
            env_or_default("INTERNAL_API_TOKEN", ""))
    };
    int connect_timeout_ms{env_int_or_default("OJ_AGENT_CONNECT_TIMEOUT_MS", 3000)};
    int read_timeout_ms{env_int_or_default("OJ_AGENT_READ_TIMEOUT_MS", 60000)};
};

struct RedisConfig {
    const char* host{env_or_default("OJ_REDIS_HOST", "127.0.0.1")};
    int port{env_int_or_default("OJ_REDIS_PORT", 6379)};
    int db{env_int_or_default("OJ_REDIS_DB", 0)};
    const char* password{env_or_default("OJ_REDIS_PASSWORD", "")};
    int socket_timeout_ms{env_int_or_default("OJ_REDIS_TIMEOUT_MS", 5000)};
    long long problem_list_ttl_seconds{env_int_or_default("OJ_PROBLEM_LIST_TTL_SECONDS", 60)};
    long long assignment_list_ttl_seconds{env_int_or_default("OJ_ASSIGNMENT_LIST_TTL_SECONDS", 60)};
    long long assignment_detail_ttl_seconds{env_int_or_default("OJ_ASSIGNMENT_DETAIL_TTL_SECONDS", 60)};
    long long assignment_leaderboard_ttl_seconds{env_int_or_default("OJ_ASSIGNMENT_LEADERBOARD_TTL_SECONDS", 15)};
    const char* submission_queue_key{env_or_default("OJ_SUBMISSION_QUEUE_KEY", "oj:queue:submissions")};
};

struct RabbitMqConfig {
    const char* host{env_or_default("OJ_RABBITMQ_HOST", "127.0.0.1")};
    int port{env_int_or_default("OJ_RABBITMQ_PORT", 5672)};
    const char* username{env_or_default("OJ_RABBITMQ_USER", "oj")};
    const char* password{env_or_default("OJ_RABBITMQ_PASSWORD", "oj_pass")};
    const char* vhost{env_or_default("OJ_RABBITMQ_VHOST", "/")};

    const char* judge_exchange{
        env_or_default("OJ_RABBITMQ_JUDGE_EXCHANGE", "oj.judge.exchange")
    };

    const char* judge_queue{
        env_or_default("OJ_RABBITMQ_JUDGE_QUEUE", "oj.judge.q")
    };

    const char* judge_retry_queue{
        env_or_default("OJ_RABBITMQ_JUDGE_RETRY_QUEUE", "oj.judge.retry.q")
    };

    const char* judge_dead_letter_queue{
        env_or_default("OJ_RABBITMQ_JUDGE_DEAD_QUEUE", "oj.judge.dead.q")
    };

    const char* judge_routing_key{
        env_or_default("OJ_RABBITMQ_JUDGE_ROUTING_KEY", "judge.submit")
    };

    const char* judge_retry_routing_key{
        env_or_default("OJ_RABBITMQ_JUDGE_RETRY_ROUTING_KEY", "judge.retry")
    };

    const char* judge_dead_letter_routing_key{
        env_or_default("OJ_RABBITMQ_JUDGE_DEAD_ROUTING_KEY", "judge.dead")
    };

    int prefetch_count{
        env_int_or_default("OJ_RABBITMQ_PREFETCH", 8)
    };

    int max_retry_count{
        env_int_or_default("OJ_RABBITMQ_MAX_RETRY_COUNT", 3)
    };

    int retry_delay_ms{
        env_int_or_default("OJ_RABBITMQ_RETRY_DELAY_MS", 3000)
    };
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

struct MonitorServiceConfig {
    const char* host{env_or_default("OJ_MONITOR_HOST", "127.0.0.1")};
    int port{env_int_or_default("OJ_MONITOR_PORT", 18090)};
    const char* summary_api_path{
        env_or_default("OJ_MONITOR_SUMMARY_API_PATH", "/api/monitor/summary")};
    const char* submissions_api_path{
        env_or_default("OJ_MONITOR_SUBMISSIONS_API_PATH", "/api/monitor/submissions")};
    int connect_timeout_ms{env_int_or_default("OJ_MONITOR_CONNECT_TIMEOUT_MS", 3000)};
    int read_timeout_ms{env_int_or_default("OJ_MONITOR_READ_TIMEOUT_MS", 5000)};
};

struct ObjectStorageConfig {
    const char* endpoint{env_or_default("OJ_OBJECT_STORAGE_ENDPOINT", "http://minio:9000")};
    const char* access_key{env_or_default("OJ_OBJECT_STORAGE_ACCESS_KEY", "minioadmin")};
    const char* secret_key{env_or_default("OJ_OBJECT_STORAGE_SECRET_KEY", "minioadmin123")};
    const char* bucket{env_or_default("OJ_OBJECT_STORAGE_BUCKET", "oj-testdata")};
    const char* alias{env_or_default("OJ_OBJECT_STORAGE_ALIAS", "ojminio")};
};


} // namespace oj::common
