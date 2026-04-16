#pragma once

#include "common/platform_types.h"
#include "common/protocol.hpp"
#include "services/oj_server/mysql_client.h"

#include <optional>
#include <vector>

namespace oj::server {

class ProblemRepository {
public:
    ProblemRepository();
    explicit ProblemRepository(MySqlClient mysql_client);

    std::vector<oj::common::ProblemSummary> list() const;
    std::vector<oj::protocol::ProblemMeta> list_problem_meta() const;
    std::optional<oj::protocol::ProblemDetail> find_detail(std::int64_t problem_id) const;
    std::vector<oj::protocol::TestCase> load_test_cases(std::int64_t problem_id) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
