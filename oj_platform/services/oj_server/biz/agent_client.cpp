#include "services/oj_server/biz/agent_client.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <algorithm>

namespace oj::server {

namespace {

struct ParsedUrl {
    std::string host;
    int port{80};
    std::string path{"/"};
};

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

ParsedUrl parse_http_url(const std::string& base_url, const std::string& path) {
    constexpr std::string_view prefix = "http://";
    if (base_url.rfind(prefix.data(), 0) != 0) {
        throw std::runtime_error("only http agent service URLs are supported");
    }

    std::string rest = base_url.substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash != std::string::npos) {
        rest = rest.substr(0, slash);
    }

    ParsedUrl parsed;
    const auto colon = rest.find(':');
    if (colon == std::string::npos) {
        parsed.host = rest;
    } else {
        parsed.host = rest.substr(0, colon);
        parsed.port = std::stoi(rest.substr(colon + 1));
    }
    parsed.path = path.empty() ? "/" : path;
    return parsed;
}

int connect_to_agent(const ParsedUrl& url, const oj::common::AgentServiceConfig& config) {
    ::addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ::addrinfo* result = nullptr;
    const auto port_text = std::to_string(url.port);
    const int rc = ::getaddrinfo(url.host.c_str(), port_text.c_str(), &hints, &result);
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
        connect_timeout.tv_sec = config.connect_timeout_ms / 1000;
        connect_timeout.tv_usec = (config.connect_timeout_ms % 1000) * 1000;
        ::setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &connect_timeout, sizeof(connect_timeout));

        ::timeval read_timeout{};
        read_timeout.tv_sec = config.read_timeout_ms / 1000;
        read_timeout.tv_usec = (config.read_timeout_ms % 1000) * 1000;
        ::setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));

        if (::connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            ::freeaddrinfo(result);
            return sockfd;
        }

        ::close(sockfd);
        sockfd = -1;
    }

    ::freeaddrinfo(result);
    throw std::runtime_error(std::string{"failed to connect agent service: "} + std::strerror(errno));
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

std::string decode_chunked_body(const std::string& body) {
    std::string decoded;
    std::size_t cursor = 0;
    while (cursor < body.size()) {
        const auto line_end = body.find("\r\n", cursor);
        if (line_end == std::string::npos) {
            throw std::runtime_error("invalid chunked response from agent service");
        }
        const auto hex_size = body.substr(cursor, line_end - cursor);
        const auto semicolon = hex_size.find(';');
        const auto size_text = hex_size.substr(0, semicolon);
        const auto chunk_size = static_cast<std::size_t>(std::stoull(size_text, nullptr, 16));
        cursor = line_end + 2;
        if (chunk_size == 0) {
            break;
        }
        if (cursor + chunk_size > body.size()) {
            throw std::runtime_error("truncated chunked response from agent service");
        }
        decoded.append(body, cursor, chunk_size);
        cursor += chunk_size + 2;
    }
    return decoded;
}

std::pair<int, std::string> extract_http_response(const std::string& raw) {
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("invalid response from agent service");
    }
    const auto status_line_end = raw.find("\r\n");
    std::istringstream status_stream(raw.substr(0, status_line_end));
    std::string http_version;
    int status_code = 0;
    status_stream >> http_version >> status_code;

    const auto headers = raw.substr(status_line_end + 2, header_end - status_line_end - 2);
    auto body = raw.substr(header_end + 4);
    if (headers.find("Transfer-Encoding: chunked") != std::string::npos ||
        headers.find("transfer-encoding: chunked") != std::string::npos) {
        body = decode_chunked_body(body);
    }
    return {status_code, body};
}

std::string json_string(const crow::json::rvalue& value, const char* key) {
    if (!value.has(key)) {
        return {};
    }
    return std::string{value[key].s()};
}

std::int64_t json_i64(const crow::json::rvalue& value, const char* key) {
    return value.has(key) ? value[key].i() : 0;
}

int json_int(const crow::json::rvalue& value, const char* key) {
    return static_cast<int>(json_i64(value, key));
}

double json_double(const crow::json::rvalue& value, const char* key) {
    return value.has(key) ? value[key].d() : 0;
}

std::vector<std::string> json_string_list(const crow::json::rvalue& value, const char* key) {
    std::vector<std::string> items;
    if (!value.has(key)) {
        return items;
    }
    for (const auto& item : value[key]) {
        items.emplace_back(item.s());
    }
    return items;
}

std::vector<AgentSourceReference> json_source_list(
    const crow::json::rvalue& value,
    const char* key) {
    std::vector<AgentSourceReference> items;
    if (!value.has(key)) {
        return items;
    }

    for (const auto& item : value[key]) {
        AgentSourceReference source;
        source.document_id = json_string(item, "document_id");
        source.source = json_string(item, "source");
        source.title = json_string(item, "title");
        source.knowledge_point = json_string(item, "knowledge_point");
        source.chunk_index = json_int(item, "chunk_index");
        source.score = json_double(item, "score");
        items.push_back(std::move(source));
    }
    return items;
}

AgentDiagnosisResponse parse_agent_diagnosis_response(const crow::json::rvalue& json) {
    AgentDiagnosisResponse response;
    response.request_id = json_string(json, "request_id");
    response.diagnosis_id = json_string(json, "diagnosis_id");
    response.user_id = json_i64(json, "user_id");
    response.problem_id = json_i64(json, "problem_id");
    response.submission_id = json_string(json, "submission_id");
    response.judge_status = json_string(json, "judge_status");
    response.hint_level = json_int(json, "hint_level");
    response.error_type = json_string(json, "error_type");
    response.summary = json_string(json, "summary");
    response.analysis = json_string(json, "analysis");
    response.evidence = json_string_list(json, "evidence");
    response.knowledge_points = json_string_list(json, "knowledge_points");
    response.hints = json_string_list(json, "hints");
    response.sources = json_source_list(json, "sources");
    response.confidence = json_double(json, "confidence");
    response.model = json_string(json, "model");
    response.provider = json_string(json, "provider");
    response.generated_at = json_i64(json, "generated_at");
    return response;
}

std::string to_lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

void feed_sse_data(
    std::string& buffer,
    std::string_view data,
    const AgentClient::StreamEventCallback& event_callback,
    AgentDiagnosisResponse& final_response,
    bool& has_final_response) {
    buffer.append(data);
    buffer.erase(
        std::remove(buffer.begin(), buffer.end(), '\r'),
        buffer.end());

    while (true) {
        const auto event_end = buffer.find("\n\n");
        if (event_end == std::string::npos) {
            break;
        }

        const auto block = buffer.substr(0, event_end);
        buffer.erase(0, event_end + 2);
        if (block.empty()) {
            continue;
        }

        std::string event = "message";
        std::string data_json;
        std::istringstream input(block);
        std::string line;
        while (std::getline(input, line)) {
            if (line.rfind("event:", 0) == 0) {
                event = line.substr(6);
                while (!event.empty() && event.front() == ' ') {
                    event.erase(event.begin());
                }
            } else if (line.rfind("data:", 0) == 0) {
                auto part = line.substr(5);
                while (!part.empty() && part.front() == ' ') {
                    part.erase(part.begin());
                }
                if (!data_json.empty()) {
                    data_json.push_back('\n');
                }
                data_json += part;
            }
        }

        if (event_callback) {
            event_callback(event, data_json);
        }

        if (event == "done") {
            const auto json = crow::json::load(data_json);
            if (!json) {
                throw std::runtime_error("agent service stream returned invalid done JSON");
            }
            final_response = parse_agent_diagnosis_response(json);
            has_final_response = true;
        } else if (event == "error") {
            const auto json = crow::json::load(data_json);
            const auto message = json ? json_string(json, "message") : data_json;
            throw std::runtime_error(message.empty() ? "agent service stream returned error" : message);
        }
    }
}

} // namespace

AgentClient::AgentClient()
    : config_{} {}

AgentClient::AgentClient(oj::common::AgentServiceConfig config)
    : config_(config) {}

AgentDiagnosisResponse AgentClient::create_diagnosis(
    const std::string& request_id,
    const crow::json::wvalue& payload) const {
    if (std::string{config_.internal_token}.empty()) {
        throw std::runtime_error("agent internal token is not configured");
    }

    const auto parsed_url = parse_http_url(config_.base_url, config_.diagnoses_api_path);
    const auto body = payload.dump();

    std::ostringstream request;
    request << "POST " << parsed_url.path << " HTTP/1.1\r\n"
            << "Host: " << parsed_url.host << ":" << parsed_url.port << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "X-Internal-Token: " << config_.internal_token << "\r\n"
            << "X-Request-Id: " << request_id << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;

    SocketHandle socket{connect_to_agent(parsed_url, config_)};
    write_all(socket.get(), request.str());

    const auto [status_code, response_body] = extract_http_response(read_all(socket.get()));
    if (status_code < 200 || status_code >= 300) {
        throw std::runtime_error("agent service returned HTTP " + std::to_string(status_code) + ": " + response_body);
    }

    const auto json = crow::json::load(response_body);
    if (!json) {
        throw std::runtime_error("agent service returned invalid JSON");
    }

    return parse_agent_diagnosis_response(json);
}

AgentDiagnosisResponse AgentClient::create_diagnosis_stream(
    const std::string& request_id,
    const crow::json::wvalue& payload,
    const StreamEventCallback& event_callback) const {
    if (std::string{config_.internal_token}.empty()) {
        throw std::runtime_error("agent internal token is not configured");
    }

    const auto stream_path = std::string{config_.diagnoses_api_path} + "/stream";
    const auto parsed_url = parse_http_url(config_.base_url, stream_path);
    const auto body = payload.dump();

    std::ostringstream request;
    request << "POST " << parsed_url.path << " HTTP/1.1\r\n"
            << "Host: " << parsed_url.host << ":" << parsed_url.port << "\r\n"
            << "Accept: text/event-stream\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "X-Internal-Token: " << config_.internal_token << "\r\n"
            << "X-Request-Id: " << request_id << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;

    SocketHandle socket{connect_to_agent(parsed_url, config_)};
    write_all(socket.get(), request.str());

    std::string raw;
    std::string header_text;
    std::string body_buffer;
    char recv_buffer[4096];
    while (true) {
        const auto n = ::recv(socket.get(), recv_buffer, sizeof(recv_buffer), 0);
        if (n == 0) {
            throw std::runtime_error("agent service closed connection before headers");
        }
        if (n < 0) {
            throw std::runtime_error(std::string{"recv failed: "} + std::strerror(errno));
        }

        raw.append(recv_buffer, static_cast<std::size_t>(n));
        const auto header_end = raw.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            continue;
        }

        header_text = raw.substr(0, header_end);
        body_buffer = raw.substr(header_end + 4);
        break;
    }

    const auto status_line_end = header_text.find("\r\n");
    std::istringstream status_stream(header_text.substr(0, status_line_end));
    std::string http_version;
    int status_code = 0;
    status_stream >> http_version >> status_code;
    if (status_code < 200 || status_code >= 300) {
        body_buffer += read_all(socket.get());
        const auto headers_lower = to_lower_copy(header_text);
        if (headers_lower.find("transfer-encoding: chunked") != std::string::npos) {
            body_buffer = decode_chunked_body(body_buffer);
        }
        throw std::runtime_error("agent service returned HTTP " + std::to_string(status_code) + ": " + body_buffer);
    }

    const auto headers_lower = to_lower_copy(header_text);
    const bool is_chunked = headers_lower.find("transfer-encoding: chunked") != std::string::npos;
    AgentDiagnosisResponse final_response;
    bool has_final_response = false;
    std::string sse_buffer;

    if (is_chunked) {
        std::string chunk_buffer = std::move(body_buffer);
        bool finished = false;
        while (!finished) {
            while (true) {
                const auto line_end = chunk_buffer.find("\r\n");
                if (line_end == std::string::npos) {
                    break;
                }

                const auto size_line = chunk_buffer.substr(0, line_end);
                const auto semicolon = size_line.find(';');
                const auto size_text = size_line.substr(0, semicolon);
                std::size_t chunk_size = 0;
                try {
                    chunk_size = static_cast<std::size_t>(std::stoull(size_text, nullptr, 16));
                } catch (...) {
                    throw std::runtime_error("failed to parse chunk size from agent stream");
                }

                if (chunk_buffer.size() < line_end + 2 + chunk_size + 2) {
                    break;
                }

                const auto data_start = line_end + 2;
                if (chunk_size == 0) {
                    finished = true;
                    break;
                }

                feed_sse_data(
                    sse_buffer,
                    std::string_view{chunk_buffer.data() + data_start, chunk_size},
                    event_callback,
                    final_response,
                    has_final_response);
                chunk_buffer.erase(0, data_start + chunk_size + 2);
            }

            if (finished) {
                break;
            }

            const auto n = ::recv(socket.get(), recv_buffer, sizeof(recv_buffer), 0);
            if (n == 0) {
                break;
            }
            if (n < 0) {
                throw std::runtime_error(std::string{"recv failed: "} + std::strerror(errno));
            }
            chunk_buffer.append(recv_buffer, static_cast<std::size_t>(n));
        }
    } else {
        feed_sse_data(
            sse_buffer,
            body_buffer,
            event_callback,
            final_response,
            has_final_response);
        while (true) {
            const auto n = ::recv(socket.get(), recv_buffer, sizeof(recv_buffer), 0);
            if (n == 0) {
                break;
            }
            if (n < 0) {
                throw std::runtime_error(std::string{"recv failed: "} + std::strerror(errno));
            }
            feed_sse_data(
                sse_buffer,
                std::string_view{recv_buffer, static_cast<std::size_t>(n)},
                event_callback,
                final_response,
                has_final_response);
        }
    }

    if (!has_final_response) {
        throw std::runtime_error("agent service stream ended without final diagnosis");
    }
    return final_response;
}

} // namespace oj::server
