#include "services/oj_server/problem_importer.h"

#include "common/object_storage_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace oj::server {
namespace {
std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

std::string strip_quotes(std::string value) {
    value = trim(value);

    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();

        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }

    return value;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_binary_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to write file: " + path.string());
    }

    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        throw std::runtime_error("failed to write complete file: " + path.string());
    }
}

std::string random_suffix() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    std::random_device rd;
    std::mt19937_64 rng(rd());
    const auto random_value = rng();

    return std::to_string(now) + "_" + std::to_string(random_value);
}

std::string shell_quote(const std::filesystem::path& path) {
    const std::string value = path.string();

    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');

    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }

    quoted.push_back('\'');
    return quoted;
}

void validate_zip_entry_name(const std::string& name) {
    if (name.empty()) {
        throw std::runtime_error("invalid empty zip entry name");
    }

    if (name.front() == '/') {
        throw std::runtime_error("zip entry uses absolute path: " + name);
    }

    if (name.find('\\') != std::string::npos) {
        throw std::runtime_error("zip entry contains backslash path: " + name);
    }

    std::filesystem::path path{name};
    for (const auto& part : path) {
        const auto part_string = part.string();
        if (part_string == "..") {
            throw std::runtime_error("zip entry contains path traversal: " + name);
        }
    }
}

void validate_zip_entries(const std::filesystem::path& zip_path) {
    const auto list_file =
        std::filesystem::temp_directory_path() / ("oj_zip_list_" + random_suffix() + ".txt");

    const std::string command =
        "unzip -Z1 " + shell_quote(zip_path) + " > " + shell_quote(list_file);

    const int code = std::system(command.c_str());
    if (code != 0) {
        std::filesystem::remove(list_file);
        throw std::runtime_error("failed to inspect zip package");
    }

    std::ifstream input(list_file, std::ios::in);
    if (!input) {
        std::filesystem::remove(list_file);
        throw std::runtime_error("failed to read zip entry list");
    }

    std::string line;
    bool has_entry = false;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        has_entry = true;
        validate_zip_entry_name(line);
    }

    std::filesystem::remove(list_file);

    if (!has_entry) {
        throw std::runtime_error("zip package is empty");
    }
}

void unzip_package(
    const std::filesystem::path& zip_path,
    const std::filesystem::path& output_dir) {
    validate_zip_entries(zip_path);

    std::filesystem::create_directories(output_dir);

    const std::string command =
        "unzip -q " + shell_quote(zip_path) + " -d " + shell_quote(output_dir);

    const int code = std::system(command.c_str());
    if (code != 0) {
        throw std::runtime_error("failed to unzip problem package");
    }
}

std::filesystem::path find_package_root(const std::filesystem::path& unzip_dir) {
    const auto direct_yaml = unzip_dir / "problem.yaml";
    const auto direct_md = unzip_dir / "problem.md";
    const auto direct_testdata = unzip_dir / "testdata";

    if (std::filesystem::exists(direct_yaml) &&
        std::filesystem::exists(direct_md) &&
        std::filesystem::is_directory(direct_testdata)) {
        return unzip_dir;
    }

    std::vector<std::filesystem::path> candidate_dirs;
    for (const auto& entry : std::filesystem::directory_iterator(unzip_dir)) {
        if (entry.is_directory()) {
            candidate_dirs.push_back(entry.path());
        }
    }

    if (candidate_dirs.size() == 1) {
        const auto root = candidate_dirs.front();
        if (std::filesystem::exists(root / "problem.yaml") &&
            std::filesystem::exists(root / "problem.md") &&
            std::filesystem::is_directory(root / "testdata")) {
            return root;
        }
    }

    throw std::runtime_error(
        "invalid problem package: expected problem.yaml, problem.md and testdata/");
}

struct ParsedYaml {
    std::string pid;
    std::string title;
    std::vector<std::string> tags;
    int time_limit_ms{1000};
    int memory_limit_mb{256};
};

int parse_time_ms(const std::string& raw_value) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return 1000;
    }

    std::string number;
    std::string unit;

    for (char ch : value) {
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
            number.push_back(ch);
        } else if (!std::isspace(static_cast<unsigned char>(ch))) {
            unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }

    if (number.empty()) {
        throw std::runtime_error("invalid time limit: " + raw_value);
    }

    const double amount = std::stod(number);

    if (unit.empty() || unit == "s" || unit == "sec" || unit == "second" || unit == "seconds") {
        return static_cast<int>(amount * 1000.0);
    }

    if (unit == "ms" || unit == "millisecond" || unit == "milliseconds") {
        return static_cast<int>(amount);
    }

    throw std::runtime_error("unsupported time unit: " + raw_value);
}

int parse_memory_mb(const std::string& raw_value) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return 256;
    }

    std::string number;
    std::string unit;

    for (char ch : value) {
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
            number.push_back(ch);
        } else if (!std::isspace(static_cast<unsigned char>(ch))) {
            unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }

    if (number.empty()) {
        throw std::runtime_error("invalid memory limit: " + raw_value);
    }

    const double amount = std::stod(number);

    if (unit.empty() || unit == "m" || unit == "mb" || unit == "mib") {
        return static_cast<int>(amount);
    }

    if (unit == "k" || unit == "kb" || unit == "kib") {
        return static_cast<int>((amount + 1023.0) / 1024.0);
    }

    if (unit == "g" || unit == "gb" || unit == "gib") {
        return static_cast<int>(amount * 1024.0);
    }

    throw std::runtime_error("unsupported memory unit: " + raw_value);
}

// 用项目约定的简化规则解析 problem.yaml，提取标题、限制和标签信息。
ParsedYaml parse_problem_yaml(const std::filesystem::path& yaml_path) {
    const std::string content = read_text_file(yaml_path);

    ParsedYaml result;
    bool in_tag_list = false;

    std::istringstream input(content);
    std::string line;

    while (std::getline(input, line)) {
        const std::string stripped = trim(line);

        if (stripped.empty() || starts_with(stripped, "#")) {
            continue;
        }

        if (starts_with(stripped, "-")) {
            if (in_tag_list) {
                std::string tag = trim(stripped.substr(1));
                tag = strip_quotes(tag);
                if (!tag.empty()) {
                    result.tags.push_back(tag);
                }
            }
            continue;
        }

        in_tag_list = false;

        const auto colon = stripped.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = trim(stripped.substr(0, colon));
        std::string value = trim(stripped.substr(colon + 1));
        value = strip_quotes(value);

        if (key == "pid") {
            result.pid = value;
        } else if (key == "title") {
            result.title = value;
        } else if (key == "time") {
            result.time_limit_ms = parse_time_ms(value);
        } else if (key == "memory") {
            result.memory_limit_mb = parse_memory_mb(value);
        } else if (key == "tag" || key == "tags") {
            in_tag_list = true;

            if (!value.empty() && value != "[]") {
                // 支持简单写法：tag: 图论
                // 完整 YAML 列表仍推荐：
                // tag:
                //   - 图论
                result.tags.push_back(value);
            }
        }
    }

    if (result.title.empty()) {
        throw std::runtime_error("problem.yaml missing title");
    }

    if (result.time_limit_ms <= 0) {
        throw std::runtime_error("invalid time limit");
    }

    if (result.memory_limit_mb <= 0) {
        throw std::runtime_error("invalid memory limit");
    }

    return result;
}

bool has_extension(const std::filesystem::path& path, const std::string& extension) {
    return path.extension().string() == extension;
}

// 校验 testdata 中的输入输出文件编号，并按题目包规则恢复测试点列表。
int parse_case_no_from_stem(const std::filesystem::path& path) {
    const std::string stem = path.stem().string();
    if (stem.empty()) {
        throw std::runtime_error("invalid testcase filename: " + path.filename().string());
    }

    for (char ch : stem) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            throw std::runtime_error("testcase filename must be numeric: " + path.filename().string());
        }
    }

    return std::stoi(stem);
}

std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct ParsedTestcaseFilename {
    int case_no{};
    bool is_input{};
    std::string normalized_filename;
};

ParsedTestcaseFilename parse_testcase_filename(const std::string& filename) {
    if (filename.empty()) {
        throw std::runtime_error("testcase filename is required");
    }

    const std::filesystem::path path{filename};
    if (path.is_absolute() ||
        path.filename() != path ||
        filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        throw std::runtime_error("testcase filename must not contain directories");
    }

    const auto extension = ascii_lower(path.extension().string());
    if (extension != ".in" && extension != ".out") {
        throw std::runtime_error("testcase filename must end with .in or .out");
    }

    const int case_no = parse_case_no_from_stem(path);
    return ParsedTestcaseFilename{
        case_no,
        extension == ".in",
        std::to_string(case_no) + extension,
    };
}

std::filesystem::path testcase_staging_root() {
    return std::filesystem::temp_directory_path() / "oj_platform_testcase_staging";
}

std::filesystem::path testcase_staging_path(std::int64_t problem_id, int case_no, bool is_input) {
    return testcase_staging_root() /
           ("problem_" + std::to_string(problem_id)) /
           ("case_" + std::to_string(case_no) + (is_input ? ".in" : ".out"));
}

void cleanup_testcase_staging(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path) {
    std::error_code ignored;
    std::filesystem::remove(input_path, ignored);
    std::filesystem::remove(output_path, ignored);

    const auto case_dir = input_path.parent_path();
    const auto problem_dir = case_dir.parent_path();

    std::filesystem::remove(case_dir, ignored);
    std::filesystem::remove(problem_dir, ignored);
}

ImportedProblem::TestCase build_appended_testcase(
    std::int64_t problem_id,
    int case_no,
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path) {
    ImportedProblem::TestCase testcase;
    testcase.case_no = case_no;
    testcase.is_sample = false;
    testcase.input_sha256 = oj::common::sha256_file(input_path);
    testcase.output_sha256 = oj::common::sha256_file(output_path);
    testcase.input_size_bytes = oj::common::file_size_bytes(input_path);
    testcase.output_size_bytes = oj::common::file_size_bytes(output_path);
    testcase.input_object_key =
        "problems/" + std::to_string(problem_id) + "/testcases/case_" + std::to_string(case_no) + ".in";
    testcase.output_object_key =
        "problems/" + std::to_string(problem_id) + "/testcases/case_" + std::to_string(case_no) + ".out";
    return testcase;
}

// 校验 testdata 中的输入输出文件配对关系，并按编号顺序恢复测试点列表。
std::vector<ImportedProblem::TestCase> load_testcases(
    const std::filesystem::path& testdata_dir,
    int sample_count) {
    if (!std::filesystem::is_directory(testdata_dir)) {
        throw std::runtime_error("missing testdata directory");
    }

    std::map<int, std::filesystem::path> input_files;
    std::map<int, std::filesystem::path> output_files;

    for (const auto& entry : std::filesystem::directory_iterator(testdata_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto path = entry.path();

        if (has_extension(path, ".in")) {
            const int case_no = parse_case_no_from_stem(path);
            input_files[case_no] = path;
        } else if (has_extension(path, ".out")) {
            const int case_no = parse_case_no_from_stem(path);
            output_files[case_no] = path;
        }
    }

    if (input_files.empty()) {
        throw std::runtime_error("no input testcases found in testdata/");
    }

    for (const auto& [case_no, input_path] : input_files) {
        if (!output_files.count(case_no)) {
            throw std::runtime_error("missing output file for testcase " + std::to_string(case_no));
        }
    }

    for (const auto& [case_no, output_path] : output_files) {
        if (!input_files.count(case_no)) {
            throw std::runtime_error("missing input file for testcase " + std::to_string(case_no));
        }
    }

    std::vector<ImportedProblem::TestCase> testcases;
    testcases.reserve(input_files.size());

    const oj::common::ObjectStorageClient storage_client;
    const auto batch_prefix = "imports/" + random_suffix();
    std::vector<std::string> uploaded_object_keys;

    try {
        for (const auto& [case_no, input_path] : input_files) {
            const auto output_path = output_files.at(case_no);

            ImportedProblem::TestCase testcase;
            testcase.case_no = case_no;
            testcase.is_sample = case_no <= sample_count;
            testcase.input_sha256 = oj::common::sha256_file(input_path);
            testcase.output_sha256 = oj::common::sha256_file(output_path);
            testcase.input_size_bytes = oj::common::file_size_bytes(input_path);
            testcase.output_size_bytes = oj::common::file_size_bytes(output_path);
            testcase.input_object_key = batch_prefix + "/case_" + std::to_string(case_no) + ".in";
            testcase.output_object_key = batch_prefix + "/case_" + std::to_string(case_no) + ".out";

            storage_client.upload_file(input_path, testcase.input_object_key);
            uploaded_object_keys.push_back(testcase.input_object_key);

            storage_client.upload_file(output_path, testcase.output_object_key);
            uploaded_object_keys.push_back(testcase.output_object_key);

            testcases.push_back(std::move(testcase));
        }
    } catch (...) {
        for (const auto& object_key : uploaded_object_keys) {
            try {
                storage_client.delete_object(object_key);
            } catch (...) {
            }
        }
        throw;
    }

    std::sort(testcases.begin(), testcases.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.case_no < rhs.case_no;
    });

    return testcases;
}

// 从解压后的题目目录中组装出可直接入库的题目完整数据结构。
ImportedProblem parse_problem_package(
    const std::filesystem::path& package_root,
    int sample_count) {
    const auto yaml_path = package_root / "problem.yaml";
    const auto markdown_path = package_root / "problem.md";
    const auto testdata_dir = package_root / "testdata";

    if (!std::filesystem::exists(yaml_path)) {
        throw std::runtime_error("missing problem.yaml");
    }

    if (!std::filesystem::exists(markdown_path)) {
        throw std::runtime_error("missing problem.md");
    }

    if (!std::filesystem::is_directory(testdata_dir)) {
        throw std::runtime_error("missing testdata directory");
    }

    const auto yaml = parse_problem_yaml(yaml_path);

    ImportedProblem problem;
    problem.title = yaml.title;
    problem.time_limit_ms = yaml.time_limit_ms;
    problem.memory_limit_mb = yaml.memory_limit_mb;
    problem.checker_type = "default";
    problem.statement_markdown = read_text_file(markdown_path);
    problem.tags = yaml.tags;
    problem.testcases = load_testcases(testdata_dir, sample_count);

    if (problem.statement_markdown.empty()) {
        throw std::runtime_error("problem.md is empty");
    }

    if (problem.testcases.empty()) {
        throw std::runtime_error("no testcases found");
    }

    return problem;
}

class TempDirectory {
public:
    explicit TempDirectory(std::filesystem::path path)
        : path_(std::move(path)) {
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace

ProblemImporter::ProblemImporter(ProblemRepository repository)
    : repository_(std::move(repository)) {}

// 校验并解压上传的 ZIP 题目包，然后转交目录导入逻辑继续处理。
ProblemImporter::ImportResult ProblemImporter::import_zip_body(
    const std::string& zip_body,
    int sample_count) const {
    if (zip_body.empty()) {
        throw std::runtime_error("zip body is empty");
    }

    if (sample_count < 0 || sample_count > 100) {
        throw std::runtime_error("invalid sample_count");
    }

    const auto temp_root =
        std::filesystem::temp_directory_path() / ("oj_import_" + random_suffix());

    TempDirectory temp_dir{temp_root};

    const auto zip_path = temp_dir.path() / "package.zip";
    const auto unzip_dir = temp_dir.path() / "unzipped";

    write_binary_file(zip_path, zip_body);
    unzip_package(zip_path, unzip_dir);

    const auto package_root = find_package_root(unzip_dir);
    return import_from_directory(package_root, sample_count);
}

ProblemImporter::AppendTestcaseFileResult ProblemImporter::append_testcase_file_body(
    std::int64_t problem_id,
    const std::string& filename,
    const std::string& file_body) const {
    if (problem_id <= 0) {
        throw std::runtime_error("problem id must be positive");
    }

    if (!repository_.find_detail(problem_id)) {
        throw std::runtime_error("problem not found");
    }

    const auto parsed = parse_testcase_filename(filename);
    if (repository_.testcase_exists(problem_id, parsed.case_no)) {
        throw std::runtime_error("testcase already exists");
    }

    const auto current_path = testcase_staging_path(problem_id, parsed.case_no, parsed.is_input);
    const auto paired_path = testcase_staging_path(problem_id, parsed.case_no, !parsed.is_input);

    std::filesystem::create_directories(current_path.parent_path());
    write_binary_file(current_path, file_body);

    AppendTestcaseFileResult result;
    result.problem_id = problem_id;
    result.case_no = parsed.case_no;
    result.filename = parsed.normalized_filename;

    if (!std::filesystem::is_regular_file(paired_path)) {
        result.paired = false;
        result.message =
            "file staged, waiting for " +
            std::to_string(parsed.case_no) +
            (parsed.is_input ? ".out" : ".in");
        return result;
    }

    const auto input_path = parsed.is_input ? current_path : paired_path;
    const auto output_path = parsed.is_input ? paired_path : current_path;
    const auto testcase = build_appended_testcase(problem_id, parsed.case_no, input_path, output_path);

    const oj::common::ObjectStorageClient storage_client;
    std::vector<std::string> uploaded_object_keys;

    try {
        storage_client.upload_file(input_path, testcase.input_object_key);
        uploaded_object_keys.push_back(testcase.input_object_key);

        storage_client.upload_file(output_path, testcase.output_object_key);
        uploaded_object_keys.push_back(testcase.output_object_key);

        repository_.append_testcase(problem_id, testcase);
    } catch (...) {
        for (const auto& object_key : uploaded_object_keys) {
            try {
                storage_client.delete_object(object_key);
            } catch (...) {
            }
        }
        throw;
    }

    cleanup_testcase_staging(input_path, output_path);

    result.paired = true;
    result.message = "testcase appended successfully";
    return result;
}

// 从题目目录解析内容、自动分配题号并将整道题原子性写入数据库。
ProblemImporter::ImportResult ProblemImporter::import_from_directory(
    const std::filesystem::path& root,
    int sample_count) const {
    auto problem = parse_problem_package(root, sample_count);

    // 自动分配从 1000 开始的未使用题号。
    problem.id = repository_.allocate_problem_id(1000);

    repository_.import_problem(problem);

    ImportResult result;
    result.problem_id = problem.id;
    result.title = problem.title;
    result.testcase_count = static_cast<int>(problem.testcases.size());

    return result;
}

} // namespace oj::server
