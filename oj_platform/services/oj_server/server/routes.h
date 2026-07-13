#pragma once

namespace crow {
template <typename... Middlewares>
class Crow;
}

namespace oj::server {

void register_routes(crow::Crow<>& app);

} // namespace oj::server
