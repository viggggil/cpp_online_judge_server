#pragma once

namespace crow {
template <typename... Middlewares>
class Crow;
}

namespace oj::worker {

void register_routes(crow::Crow<>& app);

} // namespace oj::worker
