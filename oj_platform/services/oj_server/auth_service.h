#pragma once

#include <optional>
#include <string>

#include "services/oj_server/mysql_client.h"

namespace oj::server {

struct AuthenticatedUser {
    std::string username;
};

class AuthService {
public:
    AuthService();
    explicit AuthService(MySqlClient mysql_client);

    std::string register_user(const std::string& username, const std::string& password) const;
    std::string login_user(const std::string& username, const std::string& password) const;
    std::optional<AuthenticatedUser> verify_token(const std::string& token) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server