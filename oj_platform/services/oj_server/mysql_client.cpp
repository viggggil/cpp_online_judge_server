#include "services/oj_server/mysql_client.h"

#include "common/path_utils.h"

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>
#include <mysql_driver.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <condition_variable>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace oj::server {

namespace {

std::once_flag g_schema_init_once;

std::string trim_copy(std::string text) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

// 在不引入完整 SQL 解析器的前提下，把 schema.sql 粗略拆成可逐条执行的语句。
std::vector<std::string> split_sql_statements(const std::string& script) {
    std::vector<std::string> statements;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (std::size_t i = 0; i < script.size(); ++i) {
        const char ch = script[i];
        const char next = (i + 1 < script.size()) ? script[i + 1] : '\0';

        if (!in_single_quote && !in_double_quote && ch == '-' && next == '-') {
            while (i < script.size() && script[i] != '\n') {
                ++i;
            }
            current.push_back('\n');
            continue;
        }

        if (!in_single_quote && !in_double_quote && ch == '#') {
            while (i < script.size() && script[i] != '\n') {
                ++i;
            }
            current.push_back('\n');
            continue;
        }

        if (ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        }

        if (ch == ';' && !in_single_quote && !in_double_quote) {
            auto statement = trim_copy(current);
            if (!statement.empty()) {
                statements.push_back(std::move(statement));
            }
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    auto tail = trim_copy(current);
    if (!tail.empty()) {
        statements.push_back(std::move(tail));
    }
    return statements;
}

} // namespace

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

std::filesystem::path MySqlClient::resolve_schema_path() const {
    return oj::common::resolve_project_path(std::filesystem::path{"sql"} / "schema.sql");
}

// 首次建连时自动执行 schema.sql，保证开发和测试环境具备所需表结构。
void MySqlClient::initialize_schema(sql::Connection& connection) const {
    std::call_once(g_schema_init_once, [&]() {
        const auto schema_path = resolve_schema_path();
        if (!std::filesystem::exists(schema_path)) {
            throw std::runtime_error("schema.sql not found: " + schema_path.string());
        }

        std::ifstream input(schema_path, std::ios::in | std::ios::binary);
        if (!input) {
            throw std::runtime_error("failed to open schema.sql: " + schema_path.string());
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        auto statements = split_sql_statements(buffer.str());
        auto statement = std::unique_ptr<sql::Statement>{connection.createStatement()};
        for (const auto& sql_text : statements) {
            statement->execute(sql_text);
        }
    });
}

// 建立一条新的 MySQL 连接，并完成建库、选库与会话初始化。
std::unique_ptr<sql::Connection> MySqlClient::open_new_connection() const {
    try {
        auto* driver = sql::mysql::get_driver_instance();
        auto connection = std::unique_ptr<sql::Connection>{
            driver->connect(build_uri(), config_.username, config_.password)};
        if (!connection) {
            throw std::runtime_error("failed to create mysql connection");
        }

        connection->setClientOption("OPT_RECONNECT", &config_.auto_reconnect);

        {
            auto statement = std::unique_ptr<sql::Statement>{connection->createStatement()};
            statement->execute("CREATE DATABASE IF NOT EXISTS `" + std::string{config_.database} +
                               "` CHARACTER SET " + std::string{config_.charset} +
                               " COLLATE utf8mb4_unicode_ci");
        }

        initialize_schema(*connection);
        connection->setSchema(config_.database);

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

// 从连接池借出一个可用连接，必要时阻塞等待或在上限内新建连接。
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
