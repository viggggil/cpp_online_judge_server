#pragma once

#include "services/oj_server/mysql_client.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oj::server {

struct AssignmentProblemInput {
    std::int64_t problem_id{};
    std::string alias;
};

struct CreateAssignmentRequest {
    std::string title;
    std::string description_markdown;
    std::int64_t start_at{};
    std::int64_t end_at{};
    std::int64_t created_by{};
    std::vector<AssignmentProblemInput> problems;
};

struct AssignmentSummary {
    std::int64_t id{};
    std::string title;
    std::int64_t start_at{};
    std::int64_t end_at{};
    std::int64_t created_by{};
    std::int64_t created_at{};
    std::int64_t updated_at{};
    int problem_count{};
};

struct AssignmentProblemItem {
    std::int64_t problem_id{};
    std::string title;
    int display_order{};
    std::string alias;
};

struct AssignmentDetail {
    std::int64_t id{};
    std::string title;
    std::string description_markdown;
    std::int64_t start_at{};
    std::int64_t end_at{};
    std::int64_t created_by{};
    std::int64_t created_at{};
    std::int64_t updated_at{};
    std::vector<AssignmentProblemItem> problems;
};

struct UpdateAssignmentRequest {
    std::optional<std::string> title;
    std::optional<std::string> description_markdown;
    std::optional<std::int64_t> start_at;
    std::optional<std::int64_t> end_at;
};

class AssignmentRepository {
public:
    AssignmentRepository();
    explicit AssignmentRepository(MySqlClient mysql_client);

    std::int64_t create_assignment(const CreateAssignmentRequest& request) const;

    std::vector<AssignmentSummary> list_assignments() const;

    std::optional<AssignmentDetail> find_assignment_detail(
        std::int64_t assignment_id) const;

    bool assignment_exists(std::int64_t assignment_id) const;

    bool assignment_contains_problem(
        std::int64_t assignment_id,
        std::int64_t problem_id) const;
    void update_assignment(
        std::int64_t assignment_id,
        const UpdateAssignmentRequest& request) const;

    void add_assignment_problems(
        std::int64_t assignment_id,
        const std::vector<AssignmentProblemInput>& problems) const;

private:
    MySqlClient mysql_client_;
};

} // namespace oj::server
