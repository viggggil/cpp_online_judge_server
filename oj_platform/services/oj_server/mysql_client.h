#pragma once

#include "common/platform_config.h"

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cppconn/connection.h>

namespace oj::server {

class MySqlClient {
public:
    using ConnectionHandle = std::shared_ptr<sql::Connection>;

    MySqlClient();
    explicit MySqlClient(oj::common::MySqlConfig config);

    const oj::common::MySqlConfig& config() const noexcept;
    ConnectionHandle create_connection() const;

private:
    struct PoolState {
        std::mutex mutex;
        std::condition_variable condition;
        std::vector<std::unique_ptr<sql::Connection>> idle_connections;
        std::size_t total_connections{0};
    };

    oj::common::MySqlConfig config_;
    std::size_t max_pool_size_{8};
    std::shared_ptr<PoolState> pool_state_;

    std::string build_uri() const;
    std::filesystem::path resolve_schema_path() const;
    void initialize_schema(sql::Connection& connection) const;
    std::unique_ptr<sql::Connection> open_new_connection() const;
    void recycle_connection(std::unique_ptr<sql::Connection> connection) const;
};

} // namespace oj::server