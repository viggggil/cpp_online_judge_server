#include "services/oj_server/mysql_client.h"

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>
#include <mysql_driver.h>

#include <memory>
#include <sstream>
#include <stdexcept>

namespace oj::server {

MySqlClient::MySqlClient()
    : config_{} {}

MySqlClient::MySqlClient(oj::common::MySqlConfig config)
    : config_(config) {}

const oj::common::MySqlConfig& MySqlClient::config() const noexcept {
    return config_;
}

std::string MySqlClient::build_uri() const {
    std::ostringstream uri;
    uri << "tcp://" << config_.host << ':' << config_.port;
    return uri.str();
}

std::unique_ptr<sql::Connection> MySqlClient::create_connection() const {
    try {
        auto* driver = sql::mysql::get_driver_instance();
        auto connection = std::unique_ptr<sql::Connection>{
            driver->connect(build_uri(), config_.username, config_.password)};
        if (!connection) {
            throw std::runtime_error("failed to create mysql connection");
        }

        connection->setSchema(config_.database);
        connection->setClientOption("OPT_RECONNECT", &config_.auto_reconnect);

        auto statement = std::unique_ptr<sql::Statement>{connection->createStatement()};
        statement->execute("SET NAMES " + std::string{config_.charset});
        statement->execute("SET SESSION sql_mode = 'STRICT_ALL_TABLES'");
        return connection;
    } catch (const sql::SQLException& ex) {
        throw std::runtime_error("mysql connection error: " + std::string{ex.what()});
    }
}

} // namespace oj::server