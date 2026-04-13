#include "services/oj_server/problem_repository.h"

#include <crow/json.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace oj::server {

namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::optional<crow::json::rvalue> load_json_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    auto json = crow::json::load(read_text_file(path));
    if (!json) {
        throw std::runtime_error("invalid json file: " + path.string());
    }
    return json;
}

oj::protocol::ProblemMeta parse_problem_meta(const std::filesystem::path& meta_path) {
    const auto json_opt = load_json_file(meta_path);
    if (!json_opt) {
        throw std::runtime_error("meta.json not found: " + meta_path.string());
    }

    const auto& json = *json_opt;
    oj::protocol::ProblemMeta meta;
    meta.id = json.has("id") ? json["id"].i() : 0;
    meta.title = json.has("title") ? std::string{json["title"].s()} : std::string{};
    meta.time_limit_ms = json.has("time_limit_ms") ? json["time_limit_ms"].i() : 1000;
    meta.memory_limit_mb = json.has("memory_limit_mb") ? json["memory_limit_mb"].i() : 128;

    if (json.has("tags") && json["tags"].t() == crow::json::type::List) {
        for (const auto& item : json["tags"]) {
            meta.tags.emplace_back(item.s());
        }
    }

    return meta;
}

std::string infer_difficulty(const oj::protocol::ProblemMeta& meta) {
    if (std::find(meta.tags.begin(), meta.tags.end(), "hard") != meta.tags.end()) {
        return "hard";
    }
    if (std::find(meta.tags.begin(), meta.tags.end(), "medium") != meta.tags.end()) {
        return "medium";
    }
    return "easy";
}

std::vector<oj::protocol::TestCase> load_test_cases_from_directory(const std::filesystem::path& tests_dir) {
    std::vector<oj::protocol::TestCase> test_cases;
    if (!std::filesystem::exists(tests_dir)) {
        return test_cases;
    }

    for (std::size_t index = 1;; ++index) {
        const auto input_path = tests_dir / (std::to_string(index) + ".in");
        const auto output_path = tests_dir / (std::to_string(index) + ".out");
        if (!std::filesystem::exists(input_path) || !std::filesystem::exists(output_path)) {
            break;
        }

        oj::protocol::TestCase test_case;
        test_case.input = read_text_file(input_path);
        test_case.expected_output = read_text_file(output_path);
        test_cases.push_back(std::move(test_case));
    }

    return test_cases;
}

} // namespace

ProblemRepository::ProblemRepository(std::filesystem::path root)
    : root_(std::move(root)) {}

std::vector<oj::common::ProblemSummary> ProblemRepository::list() const {
    std::vector<oj::common::ProblemSummary> items;
    for (const auto& meta : list_problem_meta()) {
        items.push_back({
            std::to_string(meta.id),
            meta.title,
            infer_difficulty(meta),
        });
    }
    return items;
}

std::vector<oj::protocol::ProblemMeta> ProblemRepository::list_problem_meta() const {
    std::vector<oj::protocol::ProblemMeta> result;
    if (!std::filesystem::exists(root_)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto meta_path = entry.path() / "meta.json";
        if (!std::filesystem::exists(meta_path)) {
            continue;
        }

        result.push_back(parse_problem_meta(meta_path));
    }

    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    return result;
}

std::optional<oj::protocol::ProblemDetail> ProblemRepository::find_detail(std::int64_t problem_id) const {
    const auto problem_dir = root_ / std::to_string(problem_id);
    const auto meta_path = problem_dir / "meta.json";
    if (!std::filesystem::exists(meta_path)) {
        return std::nullopt;
    }

    const auto meta = parse_problem_meta(meta_path);
    oj::protocol::ProblemDetail detail;
    detail.id = meta.id;
    detail.title = meta.title;
    detail.time_limit_ms = meta.time_limit_ms;
    detail.memory_limit_mb = meta.memory_limit_mb;
    detail.tags = meta.tags;

    const auto statement_path = problem_dir / "statement_zh.md";
    if (std::filesystem::exists(statement_path)) {
        detail.statement = read_text_file(statement_path);
    }

    const auto test_cases = load_test_cases(problem_id);
    for (std::size_t i = 0; i < test_cases.size() && i < 2; ++i) {
        detail.samples.push_back({test_cases[i].input, test_cases[i].expected_output});
    }

    return detail;
}

std::vector<oj::protocol::TestCase> ProblemRepository::load_test_cases(std::int64_t problem_id) const {
    return load_test_cases_from_directory(root_ / std::to_string(problem_id) / "tests");
}

} // namespace oj::server

