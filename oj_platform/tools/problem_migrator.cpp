#include "common/path_utils.h"
#include "services/oj_server/mysql_client.h"

#include <crow/json.h>

#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <ctime>

namespace {

std::filesystem::path resolve_problem_root(std::filesystem::path root) {
    return oj::common::resolve_project_path(std::move(root));
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::optional<crow::json::rvalue> load_json_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    auto json = crow::json::load(read_text_file(path));
    if (!json) {
        throw std::runtime_error("invalid json file: " + path.string());
    }
    return json;
}

long long unix_now() {
    return static_cast<long long>(std::time(nullptr));
}

// 把磁盘上的单道旧格式题目目录迁移到新的 MySQL 表结构中。
void migrate_problem(sql::Connection& connection, const std::filesystem::path& problem_dir) {
    const auto meta_path = problem_dir / "meta.json";
    const auto meta_opt = load_json_file(meta_path);
    if (!meta_opt) {
        return;
    }

    const auto& meta = *meta_opt;
    const auto problem_id = meta.has("id") ? meta["id"].i() : 0;
    const auto title = meta.has("title") ? std::string{meta["title"].s()} : std::string{};
    const auto time_limit_ms = meta.has("time_limit_ms") ? meta["time_limit_ms"].i() : 1000;
    const auto memory_limit_mb = meta.has("memory_limit_mb") ? meta["memory_limit_mb"].i() : 128;
    const auto statement_path = problem_dir / "statement_zh.md";
    const auto statement_text = std::filesystem::exists(statement_path) ? read_text_file(statement_path) : std::string{};
    const auto now = unix_now();

    {
        auto stmt = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "INSERT INTO problems (id, title, time_limit_ms, memory_limit_mb, checker_type, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?) "
                "ON DUPLICATE KEY UPDATE title = VALUES(title), time_limit_ms = VALUES(time_limit_ms), "
                "memory_limit_mb = VALUES(memory_limit_mb), checker_type = VALUES(checker_type), updated_at = VALUES(updated_at)")};
        stmt->setInt64(1, problem_id);
        stmt->setString(2, title);
        stmt->setInt(3, time_limit_ms);
        stmt->setInt(4, memory_limit_mb);
        stmt->setString(5, "default");
        stmt->setInt64(6, now);
        stmt->setInt64(7, now);
        stmt->executeUpdate();
    }

    {
        auto delete_stmt = std::unique_ptr<sql::PreparedStatement>{connection.prepareStatement("DELETE FROM problem_statements WHERE problem_id = ?")};
        delete_stmt->setInt64(1, problem_id);
        delete_stmt->executeUpdate();

        auto insert_stmt = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "INSERT INTO problem_statements (problem_id, language, statement_markdown) VALUES (?, ?, ?)")};
        insert_stmt->setInt64(1, problem_id);
        insert_stmt->setString(2, "zh-CN");
        insert_stmt->setString(3, statement_text);
        insert_stmt->executeUpdate();
    }

    {
        auto delete_stmt = std::unique_ptr<sql::PreparedStatement>{connection.prepareStatement("DELETE FROM problem_tags WHERE problem_id = ?")};
        delete_stmt->setInt64(1, problem_id);
        delete_stmt->executeUpdate();

        if (meta.has("tags") && meta["tags"].t() == crow::json::type::List) {
            auto insert_stmt = std::unique_ptr<sql::PreparedStatement>{
                connection.prepareStatement("INSERT INTO problem_tags (problem_id, tag) VALUES (?, ?)")};
            for (const auto& item : meta["tags"]) {
                insert_stmt->setInt64(1, problem_id);
                insert_stmt->setString(2, std::string{item.s()});
                insert_stmt->executeUpdate();
            }
        }
    }

    {
        auto delete_stmt = std::unique_ptr<sql::PreparedStatement>{connection.prepareStatement("DELETE FROM problem_testcases WHERE problem_id = ?")};
        delete_stmt->setInt64(1, problem_id);
        delete_stmt->executeUpdate();

        auto insert_stmt = std::unique_ptr<sql::PreparedStatement>{
            connection.prepareStatement(
                "INSERT INTO problem_testcases (problem_id, case_no, input_data, expected_output, is_sample) VALUES (?, ?, ?, ?, ?)")};

        const auto tests_dir = problem_dir / "tests";
        for (std::size_t index = 1;; ++index) {
            const auto in_path = tests_dir / (std::to_string(index) + ".in");
            const auto out_path = tests_dir / (std::to_string(index) + ".out");
            if (!std::filesystem::exists(in_path) || !std::filesystem::exists(out_path)) {
                break;
            }

            insert_stmt->setInt64(1, problem_id);
            insert_stmt->setInt(2, static_cast<int>(index));
            insert_stmt->setString(3, read_text_file(in_path));
            insert_stmt->setString(4, read_text_file(out_path));
            insert_stmt->setBoolean(5, index <= 2);
            insert_stmt->executeUpdate();
        }
    }

    std::cout << "migrated problem " << problem_id << " - " << title << '\n';
}

} // namespace

int main() {
    try {
        oj::server::MySqlClient mysql_client;
        auto connection = mysql_client.create_connection();
        connection->setAutoCommit(false);

        const auto root = resolve_problem_root("problems");
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_directory()) {
                continue;
            }
            migrate_problem(*connection, entry.path());
        }

        connection->commit();
        std::cout << "problem migration completed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "problem migration failed: " << ex.what() << std::endl;
        return 1;
    }
}
