#pragma once

#include "services/oj_server/problem_repository.h"

#include <cstdint>
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

    struct AppendTestcaseFileResult {
        std::int64_t problem_id{};
        int case_no{};
        std::string filename;
        bool paired{};
        std::string message;
    };
    
    explicit ProblemImporter(ProblemRepository repository = ProblemRepository{});

    ImportResult import_zip_body(const std::string& zip_body, int sample_count = 2) const;

    AppendTestcaseFileResult append_testcase_file_body(
        std::int64_t problem_id,
        const std::string& filename,
        const std::string& file_body) const;

private:
    ProblemRepository repository_;

    ImportResult import_from_directory(
        const std::filesystem::path& root,
        int sample_count) const;
};

} // namespace oj::server
