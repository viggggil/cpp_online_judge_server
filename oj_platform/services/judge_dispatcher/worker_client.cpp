#include "services/judge_dispatcher/worker_client.h"

#include "common/protocol_json.h"
#include "services/judge_dispatcher/dispatcher_utils.h"
#include <crow/json.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sstream>
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

// 建立到 judge_worker 的 TCP 连接，并配置连接超时和读取超时。
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

// 从原始 HTTP 响应中校验状态码并提取真正的 JSON 响应体。
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

// 使用最小 HTTP 客户端把单次判题请求发送给指定的 judge_worker。
oj::protocol::JudgeResponse WorkerClient::judge(const oj::protocol::JudgeRequest& request) const {
    const auto payload = oj::common::serialize_judge_request(request);
    const auto http_request = std::string{"POST "} + endpoint_.judge_api_path + " HTTP/1.1\r\n" +
                              "Host: " + endpoint_.host + ":" + std::to_string(endpoint_.port) + "\r\n" +
                              "Content-Type: application/json\r\n" +
                              "Connection: close\r\n" +
                              "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" +
                              payload;

    SocketHandle socket_handle{connect_to_host(endpoint_)};
    write_all(socket_handle.get(), http_request);
    const auto raw_response = read_all(socket_handle.get());
    return oj::common::deserialize_judge_response(extract_http_body(raw_response));
}

WorkerPool::WorkerPool(std::vector<oj::common::JudgeWorkerEndpoint> endpoints,
                       std::chrono::milliseconds cooldown)
    : cooldown_(cooldown) {
    if (endpoints.empty()) {
        endpoints.push_back(oj::common::JudgeWorkerEndpoint{});
    }

    workers_.reserve(endpoints.size());
    for (const auto& endpoint : endpoints) {
        workers_.push_back(WorkerState{endpoint, std::chrono::steady_clock::time_point{}});
    }
}

// 依次尝试当前可用的 worker，并把失败节点暂时放入冷却期避免连续重试。
oj::protocol::JudgeResponse WorkerPool::judge(const oj::protocol::JudgeRequest& request) {
    if (workers_.empty()) {
        throw std::runtime_error("no judge workers configured");
    }

    const auto now = std::chrono::steady_clock::now();
    std::ostringstream errors;
    bool first_error = true;

    for (std::size_t attempt = 0; attempt < workers_.size(); ++attempt) {
        const auto index = (next_index_ + attempt) % workers_.size();
        auto& worker = workers_[index];
        if (worker.unavailable_until > now) {
            continue;
        }

        try {
            WorkerClient client{worker.endpoint};
            next_index_ = (index + 1) % workers_.size();
            return client.judge(request);
        } catch (const std::exception& ex) {
            worker.unavailable_until = std::chrono::steady_clock::now() + cooldown_;
            if (!first_error) {
                errors << "; ";
            }
            first_error = false;
            errors << worker.endpoint.host << ':' << worker.endpoint.port << " => " << ex.what();
        }
    }

    if (first_error) {
        throw std::runtime_error("all judge workers are temporarily unavailable");
    }
    throw std::runtime_error("all judge workers failed: " + errors.str());
}

std::size_t WorkerPool::size() const noexcept {
    return workers_.size();
}

} // namespace oj::dispatcher
