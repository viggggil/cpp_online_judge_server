#include "services/oj_server/auth_service.h"

#include "common/path_utils.h"

#include <crow.h>
#include <crow/utility.h>

#include <crypt.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace oj::server {

namespace {

struct UserRecord {
    std::string username;
    std::string password_hash;
    std::int64_t created_at{0};
};

std::mutex& users_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::filesystem::path resolve_users_path(const std::filesystem::path& relative_path) {
    return oj::common::resolve_project_path(relative_path);
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string trim_padding(std::string value) {
    while (!value.empty() && value.back() == '=') {
        value.pop_back();
    }
    return value;
}

std::string base64url_encode(const std::string& raw) {
    return trim_padding(crow::utility::base64encode_urlsafe(raw, raw.size()));
}

std::string base64url_decode(std::string encoded) {
    while (encoded.size() % 4 != 0) {
        encoded.push_back('=');
    }
    return crow::utility::base64decode(encoded);
}

std::string jwt_secret() {
    return "oj_platform_demo_jwt_secret_change_me";
}

std::string bcrypt_salt() {
    static constexpr char kAlphabet[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    unsigned char bytes[22]{};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("failed to generate random salt");
    }

    std::string suffix;
    suffix.reserve(sizeof(bytes));
    for (unsigned char b : bytes) {
        suffix.push_back(kAlphabet[b % 64]);
    }
    return "$2b$12$" + suffix;
}

std::string bcrypt_hash(const std::string& password) {
    struct crypt_data data {};
    data.initialized = 0;
    const auto salt = bcrypt_salt();
    auto* result = crypt_r(password.c_str(), salt.c_str(), &data);
    if (result == nullptr) {
        throw std::runtime_error("bcrypt hashing failed");
    }
    return std::string{result};
}

bool bcrypt_verify(const std::string& password, const std::string& hash) {
    struct crypt_data data {};
    data.initialized = 0;
    auto* result = crypt_r(password.c_str(), hash.c_str(), &data);
    return result != nullptr && hash == result;
}

void validate_username(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        throw std::runtime_error("username length must be between 3 and 32");
    }
    const auto valid = std::all_of(username.begin(), username.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-';
    });
    if (!valid) {
        throw std::runtime_error("username only supports letters, digits, '_' and '-'");
    }
}

void validate_password(const std::string& password) {
    if (password.size() < 6 || password.size() > 128) {
        throw std::runtime_error("password length must be between 6 and 128");
    }
}

std::vector<UserRecord> load_users_from_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }

    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open users file");
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (buffer.str().empty()) {
        return {};
    }

    auto json = crow::json::load(buffer.str());
    if (!json) {
        throw std::runtime_error("invalid users json file");
    }

    std::vector<UserRecord> users;
    if (!json.has("users") || json["users"].t() != crow::json::type::List) {
        return users;
    }

    for (const auto& item : json["users"]) {
        UserRecord record;
        record.username = item.has("username") ? std::string{item["username"].s()} : std::string{};
        record.password_hash = item.has("password_hash") ? std::string{item["password_hash"].s()} : std::string{};
        record.created_at = item.has("created_at") ? item["created_at"].i() : 0;
        if (!record.username.empty() && !record.password_hash.empty()) {
            users.push_back(std::move(record));
        }
    }
    return users;
}

void save_users_to_file(const std::filesystem::path& path, const std::vector<UserRecord>& users) {
    crow::json::wvalue root;
    crow::json::wvalue::list items;
    for (const auto& user : users) {
        crow::json::wvalue item;
        item["username"] = user.username;
        item["password_hash"] = user.password_hash;
        item["created_at"] = user.created_at;
        items.push_back(std::move(item));
    }
    root["users"] = std::move(items);

    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
    output << root.dump();
}

std::optional<UserRecord> find_user(const std::vector<UserRecord>& users, const std::string& username) {
    const auto it = std::find_if(users.begin(), users.end(), [&](const UserRecord& user) {
        return user.username == username;
    });
    if (it == users.end()) {
        return std::nullopt;
    }
    return *it;
}

std::string hmac_sha256(const std::string& data, const std::string& secret) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_length = 0;
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(secret.data()), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &digest_length);
    return std::string(reinterpret_cast<char*>(digest), digest_length);
}

std::string create_token(const std::string& username) {
    crow::json::wvalue header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    crow::json::wvalue payload;
    payload["sub"] = username;
    payload["iat"] = unix_now();
    payload["exp"] = unix_now() + 24 * 60 * 60;

    const auto encoded_header = base64url_encode(header.dump());
    const auto encoded_payload = base64url_encode(payload.dump());
    const auto signing_input = encoded_header + "." + encoded_payload;
    const auto signature = base64url_encode(hmac_sha256(signing_input, jwt_secret()));
    return signing_input + "." + signature;
}

std::optional<AuthenticatedUser> parse_token(const std::string& token) {
    const auto first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return std::nullopt;
    }
    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return std::nullopt;
    }

    const auto encoded_header = token.substr(0, first_dot);
    const auto encoded_payload = token.substr(first_dot + 1, second_dot - first_dot - 1);
    const auto encoded_signature = token.substr(second_dot + 1);

    const auto expected_signature = base64url_encode(hmac_sha256(encoded_header + "." + encoded_payload, jwt_secret()));
    if (expected_signature != encoded_signature) {
        return std::nullopt;
    }

    const auto payload_text = base64url_decode(encoded_payload);
    auto payload = crow::json::load(payload_text);
    if (!payload || !payload.has("sub") || !payload.has("exp")) {
        return std::nullopt;
    }

    if (payload["exp"].i() < unix_now()) {
        return std::nullopt;
    }

    return AuthenticatedUser{std::string{payload["sub"].s()}};
}

} // namespace

AuthService::AuthService(std::filesystem::path users_file)
    : users_file_(resolve_users_path(std::move(users_file))) {}

std::string AuthService::register_user(const std::string& username, const std::string& password) const {
    validate_username(username);
    validate_password(password);

    std::lock_guard<std::mutex> guard(users_mutex());
    auto users = load_users_from_file(users_file_);
    if (find_user(users, username)) {
        throw std::runtime_error("username already exists");
    }

    users.push_back(UserRecord{username, bcrypt_hash(password), unix_now()});
    save_users_to_file(users_file_, users);
    return create_token(username);
}

std::string AuthService::login_user(const std::string& username, const std::string& password) const {
    validate_username(username);
    validate_password(password);

    std::lock_guard<std::mutex> guard(users_mutex());
    const auto users = load_users_from_file(users_file_);
    const auto user = find_user(users, username);
    if (!user || !bcrypt_verify(password, user->password_hash)) {
        throw std::runtime_error("invalid username or password");
    }

    return create_token(username);
}

std::optional<AuthenticatedUser> AuthService::verify_token(const std::string& token) const {
    if (token.empty()) {
        return std::nullopt;
    }
    return parse_token(token);
}

} // namespace oj::server