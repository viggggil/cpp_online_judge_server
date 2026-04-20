#include "services/oj_server/mysql_client.h"

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>
#include <mysql_driver.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace oj::server {

MySqlClient::MySqlClient()
    : config_{},
      pool_state_(std::make_shared<PoolState>()) {}

MySqlClient::MySqlClient(oj::common::MySqlConfig config)
    : config_(config),
      pool_state_(std::make_shared<PoolState>()) {}

const oj::common::MySqlConfig& MySqlClient::config() const noexcept {
    return config_;
}

std::string MySqlClient::build_uri() const {
    std::ostringstream uri;
    uri << "tcp://" << config_.host << ':' << config_.port;
    return uri.str();
}

std::unique_ptr<sql::Connection> MySqlClient::open_new_connection() const {
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

void MySqlClient::recycle_connection(std::unique_ptr<sql::Connection> connection) const {
    if (!connection) {
        return;
    }

    std::lock_guard<std::mutex> lock(pool_state_->mutex);
    pool_state_->idle_connections.push_back(std::move(connection));
    pool_state_->condition.notify_one();
}

MySqlClient::ConnectionHandle MySqlClient::create_connection() const {
    std::unique_ptr<sql::Connection> connection;
    {
        std::unique_lock<std::mutex> lock(pool_state_->mutex);

        while (pool_state_->idle_connections.empty() && pool_state_->total_connections >= max_pool_size_) {
            pool_state_->condition.wait(lock);
        }

        if (!pool_state_->idle_connections.empty()) {
            connection = std::move(pool_state_->idle_connections.back());
            pool_state_->idle_connections.pop_back();
        } else {
            ++pool_state_->total_connections;
            lock.unlock();
            try {
                connection = open_new_connection();
            } catch (...) {
                std::lock_guard<std::mutex> rollback_lock(pool_state_->mutex);
                --pool_state_->total_connections;
                pool_state_->condition.notify_one();
                throw;
            }
        }
    }

    return ConnectionHandle(connection.release(), [pool = pool_state_](sql::Connection* raw) {
        std::unique_ptr<sql::Connection> owned(raw);
        if (!owned) {
            return;
        }

        try {
            if (owned->isClosed()) {
                std::lock_guard<std::mutex> lock(pool->mutex);
                --pool->total_connections;
                pool->condition.notify_one();
                return;
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(pool->mutex);
            --pool->total_connections;
            pool->condition.notify_one();
            return;
        }

        std::lock_guard<std::mutex> lock(pool->mutex);
        pool->idle_connections.push_back(std::move(owned));
        pool->condition.notify_one();
    });
}

} // namespace oj::server