#include "services/judge_dispatcher/dispatcher_utils.h"

#include "common/protocol.hpp"

#include <deque>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace oj::dispatcher {

namespace {

std::string trim(std::string text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

} // namespace

std::string build_worker_failure_detail(const std::string& reason) {
    if (reason.find("timed out") != std::string::npos || reason.find("timeout") != std::string::npos) {
        return "worker timeout: " + reason;
    }
    if (reason.find("invalid http response") != std::string::npos ||
        reason.find("invalid http status line") != std::string::npos ||
        reason.find("invalid json") != std::string::npos ||
        reason.find("non-200 response") != std::string::npos) {
        return "worker invalid response: " + reason;
    }
    return "worker unreachable: " + reason;
}

void mark_submission_system_error(oj::common::SubmissionResult& record,
                                  const std::string& system_message) {
    record.judge_response.final_status = oj::protocol::JudgeStatus::system_error;
    record.judge_response.compile_success = false;
    record.judge_response.system_message = system_message;
    record.status = std::string{oj::protocol::to_string(oj::protocol::JudgeStatus::system_error)};
    record.accepted = false;
    record.detail = build_worker_failure_detail(system_message);
}

std::vector<oj::common::JudgeWorkerEndpoint> parse_worker_endpoints(const std::string& text) {
    std::vector<oj::common::JudgeWorkerEndpoint> endpoints;
    static std::deque<std::string> host_storage;
    static std::deque<std::string> path_storage;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (part.empty()) {
            continue;
        }

        oj::common::JudgeWorkerEndpoint endpoint;
        const auto scheme_pos = part.find("://");
        const auto without_scheme = scheme_pos == std::string::npos ? part : part.substr(scheme_pos + 3);
        const auto slash_pos = without_scheme.find('/');
        const auto host_port = without_scheme.substr(0, slash_pos);
        const auto path = slash_pos == std::string::npos ? std::string{"/api/judge"} : without_scheme.substr(slash_pos);
        const auto colon_pos = host_port.rfind(':');
        if (colon_pos == std::string::npos) {
            throw std::runtime_error("worker endpoint missing port: " + part);
        }

        const auto host = host_port.substr(0, colon_pos);
        const auto port_text = host_port.substr(colon_pos + 1);
        if (host.empty() || port_text.empty()) {
            throw std::runtime_error("invalid worker endpoint: " + part);
        }

        host_storage.push_back(host);
        path_storage.push_back(path.empty() ? std::string{"/api/judge"} : path);
        endpoint.host = host_storage.back().c_str();
        endpoint.port = std::stoi(port_text);
        endpoint.judge_api_path = path_storage.back().c_str();
        endpoints.push_back(endpoint);
    }

    if (endpoints.empty()) {
        endpoints.push_back(oj::common::JudgeWorkerEndpoint{});
    }
    return endpoints;
}

std::vector<oj::common::JudgeWorkerEndpoint> parse_worker_endpoints_from_env() {
    std::vector<oj::common::JudgeWorkerEndpoint> endpoints;

    if (const char* combined = std::getenv("OJ_JUDGE_WORKERS"); combined != nullptr && *combined != '\0') {
        endpoints = parse_worker_endpoints(combined);
    }

    std::string numbered;
    for (int i = 1; i <= 3; ++i) {
        const std::string env_name = "OJ_JUDGE_WORKER_" + std::to_string(i);
        if (const char* value = std::getenv(env_name.c_str()); value != nullptr && *value != '\0') {
            if (!numbered.empty()) {
                numbered += ',';
            }
            numbered += value;
        }
    }

    if (!numbered.empty()) {
        const auto parsed = parse_worker_endpoints(numbered);
        endpoints.insert(endpoints.end(), parsed.begin(), parsed.end());
    }

    if (endpoints.empty()) {
        endpoints.push_back(oj::common::JudgeWorkerEndpoint{});
    }

    return endpoints;
}

RoundRobinSelector::RoundRobinSelector(std::vector<oj::common::JudgeWorkerEndpoint> endpoints)
    : endpoints_(std::move(endpoints)) {
    if (endpoints_.empty()) {
        endpoints_.push_back(oj::common::JudgeWorkerEndpoint{});
    }
}

const oj::common::JudgeWorkerEndpoint& RoundRobinSelector::next() {
    const auto index = next_index_ % endpoints_.size();
    ++next_index_;
    return endpoints_[index];
}

std::size_t RoundRobinSelector::size() const noexcept {
    return endpoints_.size();
}

} // namespace oj::dispatcher