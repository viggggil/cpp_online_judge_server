#pragma once

#include "common/platform_config.h"

#include <memory>
#include <string>

#include <cppconn/connection.h>

namespace oj::server {

class MySqlClient {
public:
    MySqlClient();
    explicit MySqlClient(oj::common::MySqlConfig config);

    const oj::common::MySqlConfig& config() const noexcept;
    std::unique_ptr<sql::Connection> create_connection() const;

private:
    oj::common::MySqlConfig config_;
    std::string build_uri() const;
};

} // namespace oj::server