#include "services/oj_server/data/mysql_client.h"

#include "common/path_utils.h"

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
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

void MySqlClient::ensure_schema_upgrades(sql::Connection& connection) const {
    connection.setSchema(config_.database);

    auto has_column = [&](const std::string& table_name, const std::string& column_name) {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "SELECT COUNT(*) AS cnt "
                "FROM information_schema.COLUMNS "
                "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND COLUMN_NAME = ?")
        };
        statement->setString(1, config_.database);
        statement->setString(2, table_name);
        statement->setString(3, column_name);
        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        return result->next() && result->getInt("cnt") > 0;
    };

    auto has_index = [&](const std::string& table_name, const std::string& index_name) {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "SELECT COUNT(*) AS cnt "
                "FROM information_schema.STATISTICS "
                "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND INDEX_NAME = ?")
        };
        statement->setString(1, config_.database);
        statement->setString(2, table_name);
        statement->setString(3, index_name);
        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        return result->next() && result->getInt("cnt") > 0;
    };

    auto has_foreign_key = [&](const std::string& table_name, const std::string& constraint_name) {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "SELECT COUNT(*) AS cnt "
                "FROM information_schema.TABLE_CONSTRAINTS "
                "WHERE TABLE_SCHEMA = ? "
                "  AND TABLE_NAME = ? "
                "  AND CONSTRAINT_NAME = ? "
                "  AND CONSTRAINT_TYPE = 'FOREIGN KEY'")
        };
        statement->setString(1, config_.database);
        statement->setString(2, table_name);
        statement->setString(3, constraint_name);
        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        return result->next() && result->getInt("cnt") > 0;
    };

    auto is_nullable_column = [&](const std::string& table_name, const std::string& column_name) {
        auto statement = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "SELECT IS_NULLABLE "
                "FROM information_schema.COLUMNS "
                "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND COLUMN_NAME = ?")
        };
        statement->setString(1, config_.database);
        statement->setString(2, table_name);
        statement->setString(3, column_name);
        auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
        return result->next() && result->getString("IS_NULLABLE") == "YES";
    };

    auto statement = std::unique_ptr<sql::Statement>{connection.createStatement()};

    if (!has_column("submissions", "assignment_id")) {
        statement->execute(
            "ALTER TABLE submissions "
            "ADD COLUMN assignment_id BIGINT NULL AFTER problem_id_text");
    }

    if (!has_index("submissions", "idx_submissions_assignment_user_problem_created")) {
        statement->execute(
            "ALTER TABLE submissions "
            "ADD INDEX idx_submissions_assignment_user_problem_created "
            "(assignment_id, user_id, problem_id, created_at DESC, id DESC)");
    }

    if (!has_foreign_key("submissions", "fk_submissions_assignment")) {
        statement->execute(
            "ALTER TABLE submissions "
            "ADD CONSTRAINT fk_submissions_assignment "
            "FOREIGN KEY (assignment_id) REFERENCES assignments(id) "
            "ON DELETE SET NULL ON UPDATE CASCADE");
    }

    statement->execute(
        "CREATE TABLE IF NOT EXISTS assignment_user_problem_stats ("
        "assignment_id BIGINT NOT NULL, "
        "user_id BIGINT NOT NULL, "
        "problem_id BIGINT NOT NULL, "
        "username_snapshot VARCHAR(64) NOT NULL, "
        "submission_count INT NOT NULL DEFAULT 0, "
        "accepted TINYINT(1) NOT NULL DEFAULT 0, "
        "first_accepted_at BIGINT NOT NULL DEFAULT 0, "
        "last_submitted_at BIGINT NOT NULL DEFAULT 0, "
        "last_status VARCHAR(64) NOT NULL DEFAULT 'UNKNOWN', "
        "score INT NOT NULL DEFAULT 0, "
        "penalty_seconds BIGINT NOT NULL DEFAULT 0, "
        "last_processed_submission_id BIGINT NOT NULL DEFAULT 0, "
        "updated_at BIGINT NOT NULL, "
        "PRIMARY KEY (assignment_id, user_id, problem_id), "
        "KEY idx_assignment_problem (assignment_id, problem_id), "
        "KEY idx_assignment_user (assignment_id, user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    statement->execute(
        "CREATE TABLE IF NOT EXISTS assignment_user_rank_stats ("
        "assignment_id BIGINT NOT NULL, "
        "user_id BIGINT NOT NULL, "
        "username_snapshot VARCHAR(64) NOT NULL, "
        "solved_count INT NOT NULL DEFAULT 0, "
        "score INT NOT NULL DEFAULT 0, "
        "penalty_seconds BIGINT NOT NULL DEFAULT 0, "
        "rank_score BIGINT NOT NULL DEFAULT 0, "
        "last_processed_submission_id BIGINT NOT NULL DEFAULT 0, "
        "updated_at BIGINT NOT NULL, "
        "PRIMARY KEY (assignment_id, user_id), "
        "KEY idx_assignment_rank (assignment_id, solved_count DESC, penalty_seconds ASC, username_snapshot ASC), "
        "KEY idx_assignment_rank_score (assignment_id, rank_score DESC)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    statement->execute(
        "CREATE TABLE IF NOT EXISTS ai_conversation ("
        "id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY, "
        "conversation_id VARCHAR(64) NOT NULL, "
        "user_id BIGINT NOT NULL, "
        "problem_id BIGINT NULL, "
        "submission_db_id BIGINT NULL, "
        "submission_id VARCHAR(64) NULL, "
        "title VARCHAR(255) NOT NULL DEFAULT '', "
        "hint_level TINYINT NOT NULL DEFAULT 2, "
        "round_count INT NOT NULL DEFAULT 0, "
        "status VARCHAR(32) NOT NULL DEFAULT 'active', "
        "last_message_at BIGINT NOT NULL DEFAULT 0, "
        "created_at BIGINT NOT NULL, "
        "updated_at BIGINT NOT NULL, "
        "UNIQUE KEY uk_ai_conversation_conversation_id (conversation_id), "
        "KEY idx_ai_conversation_user_updated (user_id, updated_at DESC, id DESC), "
        "KEY idx_ai_conversation_user_problem_updated "
        "(user_id, problem_id, updated_at DESC, id DESC), "
        "KEY idx_ai_conversation_submission (submission_db_id), "
        "CONSTRAINT fk_ai_conversation_user "
        "FOREIGN KEY (user_id) REFERENCES users(id) "
        "ON DELETE CASCADE ON UPDATE CASCADE, "
        "CONSTRAINT fk_ai_conversation_problem "
        "FOREIGN KEY (problem_id) REFERENCES problems(id) "
        "ON DELETE SET NULL ON UPDATE CASCADE, "
        "CONSTRAINT fk_ai_conversation_submission "
        "FOREIGN KEY (submission_db_id) REFERENCES submissions(id) "
        "ON DELETE SET NULL ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    statement->execute(
        "CREATE TABLE IF NOT EXISTS ai_message ("
        "id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY, "
        "message_id VARCHAR(64) NOT NULL, "
        "conversation_db_id BIGINT NOT NULL, "
        "round_no INT NOT NULL, "
        "hint_level TINYINT NOT NULL DEFAULT 2, "
        "request_id VARCHAR(64) NOT NULL DEFAULT '', "
        "user_content MEDIUMTEXT NOT NULL, "
        "assistant_content MEDIUMTEXT NOT NULL, "
        "model VARCHAR(128) NOT NULL DEFAULT '', "
        "provider VARCHAR(128) NOT NULL DEFAULT '', "
        "finish_reason VARCHAR(64) NOT NULL DEFAULT '', "
        "prompt_tokens INT NOT NULL DEFAULT 0, "
        "completion_tokens INT NOT NULL DEFAULT 0, "
        "total_tokens INT NOT NULL DEFAULT 0, "
        "latency_ms INT NOT NULL DEFAULT 0, "
        "knowledge_points_text TEXT NOT NULL, "
        "sources_json MEDIUMTEXT NULL, "
        "safety_flags_json MEDIUMTEXT NULL, "
        "error_type VARCHAR(64) NOT NULL DEFAULT '', "
        "confidence DECIMAL(5,4) NULL, "
        "created_at BIGINT NOT NULL, "
        "UNIQUE KEY uk_ai_message_message_id (message_id), "
        "UNIQUE KEY uk_ai_message_conversation_round "
        "(conversation_db_id, round_no), "
        "KEY idx_ai_message_conversation_created "
        "(conversation_db_id, created_at ASC, id ASC), "
        "KEY idx_ai_message_model_created (model, created_at DESC), "
        "CONSTRAINT fk_ai_message_conversation "
        "FOREIGN KEY (conversation_db_id) REFERENCES ai_conversation(id) "
        "ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    if (has_column("ai_conversation", "problem_id") &&
        !is_nullable_column("ai_conversation", "problem_id")) {
        if (has_foreign_key("ai_conversation", "fk_ai_conversation_problem")) {
            statement->execute(
                "ALTER TABLE ai_conversation "
                "DROP FOREIGN KEY fk_ai_conversation_problem");
        }
        statement->execute(
            "ALTER TABLE ai_conversation "
            "MODIFY COLUMN problem_id BIGINT NULL");
    }

    if (!has_foreign_key("ai_conversation", "fk_ai_conversation_problem")) {
        statement->execute(
            "ALTER TABLE ai_conversation "
            "ADD CONSTRAINT fk_ai_conversation_problem "
            "FOREIGN KEY (problem_id) REFERENCES problems(id) "
            "ON DELETE SET NULL ON UPDATE CASCADE");
    }
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
        ensure_schema_upgrades(*connection);

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
