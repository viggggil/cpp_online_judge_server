#pragma once

namespace oj::common {

struct ServiceConfig {
    int port{};
    const char* service_name{};
};

struct RedisConfig {
    const char* host{"127.0.0.1"};
    int port{6379};
    int db{0};
    const char* password{""};
    int socket_timeout_ms{100};
    long long problem_list_ttl_seconds{60};
};

} // namespace oj::common
