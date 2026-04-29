#include "services/oj_server/auth_service.h"

#include "common/platform_config.h"

#include <crow.h>
#include <crow/utility.h>

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <crypt.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace oj::server {

namespace {

struct UserRecord {
    std::string username;
    std::string password_hash;
    std::string role{"user"};
    std::int64_t created_at{0};
};

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

std::string admin_registration_code() {
    return oj::common::env_or_default("OJ_ADMIN_REGISTER_CODE", "");
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

std::optional<UserRecord> find_user(sql::Connection& connection, const std::string& username) {
    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection.prepareStatement(
            "SELECT username, password_hash, role, created_at FROM users WHERE username = ?")};
    statement->setString(1, username);
    auto result = std::unique_ptr<sql::ResultSet>{statement->executeQuery()};
    if (!result->next()) {
        return std::nullopt;
    }

    UserRecord user;
    user.username = result->getString("username");
    user.role = result->getString("role");
    user.password_hash = result->getString("password_hash");
    user.created_at = result->getInt64("created_at");
    return user;
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

std::string create_token(const std::string& username, const std::string& role){
    crow::json::wvalue header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    crow::json::wvalue payload;
    payload["sub"] = username;
    payload["iat"] = unix_now();
    payload["exp"] = unix_now() + 24 * 60 * 60;
    payload["role"] = role;

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

    std::string role = "user";
    if (payload.has("role")) {
        role = std::string{payload["role"].s()};
    }
    return AuthenticatedUser{std::string{payload["sub"].s()}, role};
}

} // namespace

AuthService::AuthService()
    : mysql_client_{} {}

AuthService::AuthService(MySqlClient mysql_client)
    : mysql_client_(std::move(mysql_client)) {}

std::string AuthService::register_user(const std::string& username, const std::string& password) const {
    validate_username(username);
    validate_password(password);

    auto connection = mysql_client_.create_connection();
    if (find_user(*connection, username)) {
        throw std::runtime_error("username already exists");
    }

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "INSERT INTO users (username, password_hash, role, created_at) VALUES (?, ?, ?, ?)")};
    statement->setString(1, username);
    statement->setString(2, bcrypt_hash(password));
    statement->setString(3, "user");
    statement->setInt64(4, unix_now());
    statement->executeUpdate();
    return create_token(username, "user");
}

std::string AuthService::register_admin(const std::string& username,
                                       const std::string& password,
                                       const std::string& admin_code) const {
    validate_username(username);
    validate_password(password);

    const auto configured_admin_code = admin_registration_code();
    if (configured_admin_code.empty()) {
        throw std::runtime_error("admin registration is disabled");
    }
    if (admin_code != configured_admin_code) {
        throw std::runtime_error("invalid admin code");
    }

    auto connection = mysql_client_.create_connection();
    if (find_user(*connection, username)) {
        throw std::runtime_error("username already exists");
    }

    auto statement = std::unique_ptr<sql::PreparedStatement>{
        connection->prepareStatement(
            "INSERT INTO users (username, password_hash, role, created_at) VALUES (?, ?, ?, ?)")};
    statement->setString(1, username);
    statement->setString(2, bcrypt_hash(password));
    statement->setString(3, "admin");
    statement->setInt64(4, unix_now());
    statement->executeUpdate();
    return create_token(username, "admin");
}

std::string AuthService::login_user(const std::string& username, const std::string& password) const {
    validate_username(username);
    validate_password(password);

    auto connection = mysql_client_.create_connection();
    const auto user = find_user(*connection, username);
    if (!user || !bcrypt_verify(password, user->password_hash)) {
        throw std::runtime_error("invalid username or password");
    }

    return create_token(user->username, user->role);
}

std::optional<AuthenticatedUser> AuthService::verify_token(const std::string& token) const {
    if (token.empty()) {
        return std::nullopt;
    }
    return parse_token(token);
}

} // namespace oj::server