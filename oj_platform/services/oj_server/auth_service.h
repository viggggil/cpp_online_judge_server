#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace oj::server {

struct AuthenticatedUser {
    std::string username;
};

class AuthService {
public:
    explicit AuthService(std::filesystem::path users_file = "runtime/users/users.json");

    std::string register_user(const std::string& username, const std::string& password) const;
    std::string login_user(const std::string& username, const std::string& password) const;
    std::optional<AuthenticatedUser> verify_token(const std::string& token) const;

private:
    std::filesystem::path users_file_;
};

} // namespace oj::server