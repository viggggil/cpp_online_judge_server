#pragma once

#include "services/oj_server/problem_repository.h"
#include <filesystem>
#include <string>

namespace oj::server {

class ProblemImporter {
public:
    struct ImportResult {
        std::int64_t problem_id{};
        std::string title;
        int testcase_count{};
    };

    explicit ProblemImporter(ProblemRepository repository = ProblemRepository{});

    ImportResult import_zip_body(const std::string& zip_body, int sample_count = 2) const;

private:
    ProblemRepository repository_;

    ImportResult import_from_directory(
        const std::filesystem::path& root,
        int sample_count) const;
};

} // namespace oj::server
