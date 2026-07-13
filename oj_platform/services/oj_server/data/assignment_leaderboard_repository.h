#pragma once

#include "common/platform_types.h"
#include "services/oj_server/data/mysql_client.h"

#include <cstdint>
#include <optional>

namespace oj::server {

class AssignmentLeaderboardRepository {
public:
    AssignmentLeaderboardRepository();
    explicit AssignmentLeaderboardRepository(MySqlClient mysql_client);

    std::optional<oj::common::AssignmentLeaderboard>find_assignment_leaderboard(std::int64_t assignment_id) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
