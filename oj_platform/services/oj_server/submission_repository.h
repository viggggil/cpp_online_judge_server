#pragma once

#include "common/platform_types.h"
#include "services/oj_server/mysql_client.h"

#include <optional>
#include <string>
#include <vector>

namespace oj::server {

struct StoredSubmission {
    oj::common::SubmissionResult result;
    std::string username;
    std::int64_t created_at{0};
    std::int64_t updated_at{0};
};

class SubmissionRepository {
public:
    SubmissionRepository();
    explicit SubmissionRepository(MySqlClient mysql_client);

    void create_submission(const std::string& submission_id,
                           const std::string& username,
                           const oj::common::SubmissionRequest& request,
                           const std::string& status,
                           const std::string& detail) const;

    void update_submission(const oj::common::SubmissionResult& result) const;

    std::optional<StoredSubmission> find_submission(const std::string& submission_id) const;
    std::optional<StoredSubmission> find_submission_for_user(const std::string& submission_id,
                                                             const std::string& username) const;
    std::vector<oj::common::SubmissionListItem> list_submissions_for_user(const std::string& username,
                                                                          std::size_t limit = 50) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server