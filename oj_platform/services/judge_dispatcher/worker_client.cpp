#include "services/judge_dispatcher/worker_client.h"

#include <crow/json.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace oj::dispatcher {

namespace {

class SocketHandle {
public:
    explicit SocketHandle(int fd) : fd_(fd) {}
    ~SocketHandle() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    int get() const { return fd_; }

private:
    int fd_{-1};
};

oj::protocol::JudgeStatus parse_judge_status(const std::string& text) {
    if (text == "OK") return oj::protocol::JudgeStatus::ok;
    if (text == "COMPILE_ERROR") return oj::protocol::JudgeStatus::compile_error;
    if (text == "RUNTIME_ERROR") return oj::protocol::JudgeStatus::runtime_error;
    if (text == "TIME_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::time_limit_exceeded;
    if (text == "MEMORY_LIMIT_EXCEEDED") return oj::protocol::JudgeStatus::memory_limit_exceeded;
    if (text == "WRONG_ANSWER") return oj::protocol::JudgeStatus::wrong_answer;
    if (text == "PRESENTATION_ERROR") return oj::protocol::JudgeStatus::presentation_error;
    return oj::protocol::JudgeStatus::system_error;
}

std::string build_request_json(const oj::protocol::JudgeRequest& request) {
    crow::json::wvalue body;
    body["submission_id"] = request.submission_id;
    body["problem_id"] = request.problem_id;
    body["language"] = std::string{oj::protocol::to_string(request.language)};
    body["source_code"] = request.source_code;
    body["time_limit_ms"] = request.time_limit_ms;
    body["memory_limit_mb"] = request.memory_limit_mb;

    crow::json::wvalue::list items;
    for (const auto& test_case : request.test_cases) {
        crow::json::wvalue item;
        item["input"] = test_case.input;
        item["expected_output"] = test_case.expected_output;
        items.push_back(std::move(item));
    }
    body["test_cases"] = std::move(items);
    return body.dump();
}

oj::protocol::JudgeResponse parse_response_json(const std::string& payload) {
    const auto json = crow::json::load(payload);
    if (!json) {
        throw std::runtime_error("judge_worker returned invalid json");
    }

    oj::protocol::JudgeResponse response;
    response.submission_id = json.has("submission_id") ? json["submission_id"].i() : 0;
    response.final_status = json.has("final_status")
                                ? parse_judge_status(std::string{json["final_status"].s()})
                                : oj::protocol::JudgeStatus::system_error;
    response.compile_success = json.has("compile_success") && json["compile_success"].b();
    response.compile_stdout = json.has("compile_stdout") ? std::string{json["compile_stdout"].s()} : std::string{};
    response.compile_stderr = json.has("compile_stderr") ? std::string{json["compile_stderr"].s()} : std::string{};
    response.total_time_used_ms = json.has("total_time_used_ms") ? json["total_time_used_ms"].i() : 0;
    response.peak_memory_used_kb = json.has("peak_memory_used_kb") ? json["peak_memory_used_kb"].i() : 0;
    response.system_message = json.has("system_message") ? std::string{json["system_message"].s()} : std::string{};

    if (json.has("test_case_results") && json["test_case_results"].t() == crow::json::type::List) {
        for (const auto& item : json["test_case_results"]) {
            oj::protocol::TestCaseResult result;
            result.status = item.has("status") ? parse_judge_status(std::string{item["status"].s()}) : oj::protocol::JudgeStatus::system_error;
            result.input = item.has("input") ? std::string{item["input"].s()} : std::string{};
            result.time_used_ms = item.has("time_used_ms") ? item["time_used_ms"].i() : 0;
            result.memory_used_kb = item.has("memory_used_kb") ? item["memory_used_kb"].i() : 0;
            result.actual_output = item.has("actual_output") ? std::string{item["actual_output"].s()} : std::string{};
            result.expected_output = item.has("expected_output") ? std::string{item["expected_output"].s()} : std::string{};
            result.error_message = item.has("error_message") ? std::string{item["error_message"].s()} : std::string{};
            response.test_case_results.push_back(std::move(result));
        }
    }

    return response;
}

int connect_to_host(const oj::common::JudgeWorkerEndpoint& endpoint) {
    ::addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ::addrinfo* result = nullptr;
    const auto port_text = std::to_string(endpoint.port);
    const int rc = ::getaddrinfo(endpoint.host, port_text.c_str(), &hints, &result);
    if (rc != 0) {
        throw std::runtime_error(std::string{"getaddrinfo failed: "} + ::gai_strerror(rc));
    }

    int sockfd = -1;
    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        ::timeval connect_timeout{};
        connect_timeout.tv_sec = endpoint.connect_timeout_ms / 1000;
        connect_timeout.tv_usec = (endpoint.connect_timeout_ms % 1000) * 1000;
        ::setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &connect_timeout, sizeof(connect_timeout));

        ::timeval read_timeout{};
        read_timeout.tv_sec = endpoint.read_timeout_ms / 1000;
        read_timeout.tv_usec = (endpoint.read_timeout_ms % 1000) * 1000;
        ::setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));

        if (::connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            ::freeaddrinfo(result);
            return sockfd;
        }

        ::close(sockfd);
        sockfd = -1;
    }

    ::freeaddrinfo(result);
    throw std::runtime_error(std::string{"failed to connect judge_worker: "} + std::strerror(errno));
}

void write_all(int fd, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            throw std::runtime_error(std::string{"send failed: "} + std::strerror(errno));
        }
        sent += static_cast<std::size_t>(n);
    }
}

std::string read_all(int fd) {
    std::string response;
    char buffer[4096];
    while (true) {
        const auto n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            throw std::runtime_error(std::string{"recv failed: "} + std::strerror(errno));
        }
        response.append(buffer, static_cast<std::size_t>(n));
    }
    return response;
}

std::string extract_http_body(const std::string& raw_response) {
    const auto header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("invalid http response from judge_worker");
    }

    const auto status_line_end = raw_response.find("\r\n");
    if (status_line_end == std::string::npos) {
        throw std::runtime_error("invalid http status line from judge_worker");
    }

    const auto status_line = raw_response.substr(0, status_line_end);
    if (status_line.find(" 200 ") == std::string::npos) {
        throw std::runtime_error("judge_worker returned non-200 response: " + status_line);
    }

    return raw_response.substr(header_end + 4);
}

} // namespace

WorkerClient::WorkerClient(oj::common::JudgeWorkerEndpoint endpoint)
    : endpoint_(endpoint) {}

oj::protocol::JudgeResponse WorkerClient::judge(const oj::protocol::JudgeRequest& request) const {
    const auto payload = build_request_json(request);
    const auto http_request = std::string{"POST "} + endpoint_.judge_api_path + " HTTP/1.1\r\n" +
                              "Host: " + endpoint_.host + ":" + std::to_string(endpoint_.port) + "\r\n" +
                              "Content-Type: application/json\r\n" +
                              "Connection: close\r\n" +
                              "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" +
                              payload;

    SocketHandle socket_handle{connect_to_host(endpoint_)};
    write_all(socket_handle.get(), http_request);
    const auto raw_response = read_all(socket_handle.get());
    return parse_response_json(extract_http_body(raw_response));
}

} // namespace oj::dispatcher